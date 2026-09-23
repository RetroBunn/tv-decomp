# TruVoice for NVDA: the binding to tvtts.dll.
# This file is covered by the GNU General Public License, version 2 or later.

"""Loads the TruVoice library, runs synthesis on a background thread and
feeds NVDA's audio player.

The library is synchronous: ``tvtts_speak_utf16`` blocks and calls back with
audio and index marks interleaved in stream order, and the callback aborts it
by returning non-zero.  That suits a screen reader -- cancellation is a flag
rather than a lock -- but it has to run somewhere other than the thread NVDA
calls ``speak`` on, hence the queue and thread here, which follow the shape of
NVDA's own eSpeak driver.
"""

import os
import queue
import threading
from ctypes import (
	CDLL,
	CFUNCTYPE,
	POINTER,
	Structure,
	c_char_p,
	c_int,
	c_int16,
	c_int32,
	c_uint32,
	c_void_p,
	c_wchar_p,
	cast,
	string_at,
)

import nvwave
from logHandler import log

#: The engine's native sample rate.  It can also run at 8000, which is what
#: the "phone optimised" voices of the original were, but 11025 is what a
#: desktop wants.
SAMPLE_RATE = 11025

TVTTS_AUDIO = 0
TVTTS_MARK = 1
TVTTS_END = 2


class TvttsEvent(Structure):
	_fields_ = [  # noqa: RUF012
		("type", c_int32),
		("count", c_uint32),
		("samples", POINTER(c_int16)),
		("mark", c_uint32),
		("samplePos", c_uint32),
	]


callbackType = CFUNCTYPE(c_int, POINTER(TvttsEvent), c_void_p)

dll = None
player = None
bgThread = None
bgQueue = None
synth = None
onIndexReached = None
isSpeaking = False
#: Audio handed to us but not yet given to the player.  It is held so that a
#: mark can be attached to the chunk it follows: see _callback.
_pending = b""
#: Marks the driver could not put in the text, because an escape after the
#: last word changes how the engine reads it.  Reported at the end instead.
_trailingMarks = ()


def _dllPath() -> str:
	return os.path.join(os.path.dirname(__file__), "tvtts.dll")


def _flush(onDone=None):
	"""Give the player everything held so far, and hang onDone off it."""
	global _pending
	if _pending:
		player.feed(_pending, onDone=onDone)
		_pending = b""
	elif onDone:
		onDone()


@callbackType
def _callback(evPtr, user):  # noqa: ARG001
	try:
		global _pending, isSpeaking
		if not isSpeaking:
			return 1
		ev = evPtr.contents
		if ev.type == TVTTS_AUDIO:
			_pending += string_at(cast(ev.samples, c_void_p), ev.count * 2)
		elif ev.type == TVTTS_MARK:
			# Everything before this mark has already been handed over, so
			# the notification belongs on the end of what is buffered.
			mark = ev.mark
			_flush(onDone=lambda mark=mark: onIndexReached(mark))
		elif ev.type == TVTTS_END:
			# player.idle() waits for that last chunk, so the held marks
			# have been reported by the time the end notification goes.
			marks = _trailingMarks
			_flush(onDone=lambda: [onIndexReached(m) for m in marks])
			player.idle()
			isSpeaking = False
			onIndexReached(None)
		return 0
	except Exception:
		log.error("TruVoice callback", exc_info=True)
		return 1


class BgThread(threading.Thread):
	def __init__(self):
		super().__init__(name=f"{self.__class__.__module__}.{self.__class__.__qualname__}")
		self.daemon = True

	def run(self):
		while True:
			func, args, kwargs = bgQueue.get()
			if not func:
				break
			try:
				func(*args, **kwargs)
			except Exception:
				log.error("Error running function from queue", exc_info=True)
			bgQueue.task_done()


def _execWhenDone(func, *args, mustBeAsync=False, **kwargs):
	if mustBeAsync or bgQueue.unfinished_tasks != 0:
		bgQueue.put((func, args, kwargs))
	else:
		func(*args, **kwargs)


def _speak(text: str, trailingMarks=(), restore=None):
	global isSpeaking, _pending, _trailingMarks
	isSpeaking = True
	_pending = b""
	_trailingMarks = trailingMarks
	dll.tvtts_speak_utf16(synth, c_wchar_p(text), _callback, None)
	_trailingMarks = ()
	# An inline pitch or rate escape changes the same state the setters
	# do and stays in effect afterwards, so a capital's raised pitch
	# would stay raised for good if it were not put back here.
	if restore is not None:
		pitch, rate = restore
		dll.tvtts_set_pitch(synth, pitch)
		dll.tvtts_set_rate(synth, rate)


def speak(text: str, trailingMarks=(), restore=None):
	_execWhenDone(_speak, text, trailingMarks, restore, mustBeAsync=True)


def stop():
	"""Drop anything queued and cut the current utterance short."""
	global isSpeaking, _pending, _trailingMarks
	# Let the callback unwind the library, then clear the queue of anything
	# not yet started.
	isSpeaking = False
	_trailingMarks = ()
	try:
		while True:
			bgQueue.get_nowait()
			bgQueue.task_done()
	except queue.Empty:
		pass
	_pending = b""
	player.stop()


def pause(switch: bool):
	player.pause(switch)


def setVoice(index: int):
	_execWhenDone(dll.tvtts_set_voice, synth, index)


def setRate(wpm: int):
	_execWhenDone(dll.tvtts_set_rate, synth, wpm)


def setPitch(value: int):
	_execWhenDone(dll.tvtts_set_pitch, synth, value)


def setVolume(value: int):
	_execWhenDone(dll.tvtts_set_volume, synth, value)


def voiceCount() -> int:
	return dll.tvtts_voice_count()


def voiceName(index: int) -> str:
	name = dll.tvtts_voice_name(index)
	return name.decode("mbcs") if name else str(index)


def voiceRate(index: int) -> int:
	return dll.tvtts_voice_rate(index)


def voicePitch(index: int) -> int:
	return dll.tvtts_voice_pitch(index)


def markSequence(index: int) -> str:
	buf = bytes(16)
	n = dll.tvtts_mark_sequence(buf, len(buf), index)
	return buf[:n].decode("mbcs") if n else ""


def breakSequence(ms: int) -> str:
	buf = bytes(16)
	n = dll.tvtts_break_sequence(buf, len(buf), max(0, ms))
	return buf[:n].decode("mbcs") if n else ""


def punctuationSequence(on: bool) -> str:
	"""Say punctuation aloud, so "Hi, there." reads its comma and period.

	The driver does not use it: NVDA does its own symbol processing, and
	the flag would outlast the utterance that set it.  It is here because
	it is part of the library's surface and the test drives it.
	"""
	buf = bytes(16)
	n = dll.tvtts_punctuation_sequence(buf, len(buf), 1 if on else 0)
	return buf[:n].decode("mbcs") if n else ""


def pitchSequence(pitch: int) -> str:
	buf = bytes(16)
	n = dll.tvtts_pitch_sequence(buf, len(buf), pitch)
	return buf[:n].decode("mbcs") if n else ""


def rateSequence(wpm: int) -> str:
	buf = bytes(16)
	n = dll.tvtts_rate_sequence(buf, len(buf), wpm)
	return buf[:n].decode("mbcs") if n else ""


def isAvailable() -> bool:
	return os.path.isfile(_dllPath())


def _bind(lib: CDLL):
	lib.tvtts_create.restype = c_void_p
	lib.tvtts_create.argtypes = [c_uint32]
	lib.tvtts_destroy.restype = None
	lib.tvtts_destroy.argtypes = [c_void_p]
	lib.tvtts_speak_utf16.restype = c_int
	lib.tvtts_speak_utf16.argtypes = [c_void_p, c_wchar_p, callbackType, c_void_p]
	for name in ("tvtts_set_voice", "tvtts_set_rate", "tvtts_set_pitch"):
		getattr(lib, name).restype = None
		getattr(lib, name).argtypes = [c_void_p, c_int]
	lib.tvtts_set_volume.restype = None
	lib.tvtts_set_volume.argtypes = [c_void_p, c_uint32]
	lib.tvtts_voice_count.restype = c_int
	lib.tvtts_voice_count.argtypes = []
	lib.tvtts_voice_name.restype = c_char_p
	lib.tvtts_voice_name.argtypes = [c_int]
	for name in ("tvtts_voice_rate", "tvtts_voice_pitch"):
		getattr(lib, name).restype = c_int
		getattr(lib, name).argtypes = [c_int]
	for name in ("tvtts_mark_sequence", "tvtts_break_sequence"):
		getattr(lib, name).restype = c_int
		getattr(lib, name).argtypes = [c_char_p, c_uint32, c_uint32]
	for name in ("tvtts_punctuation_sequence", "tvtts_pitch_sequence", "tvtts_rate_sequence"):
		getattr(lib, name).restype = c_int
		getattr(lib, name).argtypes = [c_char_p, c_uint32, c_int]


def initialize(indexCallback):
	"""@param indexCallback: called with a mark index, or None at the end of
	an utterance.  It runs on the audio player's thread, not NVDA's main one.
	"""
	global dll, player, bgThread, bgQueue, synth, onIndexReached
	import config

	dll = CDLL(_dllPath())
	_bind(dll)
	synth = dll.tvtts_create(SAMPLE_RATE)
	if not synth:
		raise RuntimeError("tvtts_create failed")
	player = nvwave.WavePlayer(
		channels=1,
		samplesPerSec=SAMPLE_RATE,
		bitsPerSample=16,
		outputDevice=config.conf["audio"]["outputDevice"],
	)
	onIndexReached = indexCallback
	bgQueue = queue.Queue()
	bgThread = BgThread()
	bgThread.start()


def terminate():
	global dll, player, bgThread, bgQueue, synth, onIndexReached
	stop()
	bgQueue.put((None, None, None))
	bgThread.join()
	dll.tvtts_destroy(synth)
	player.close()
	synth = None
	player = None
	bgThread = None
	bgQueue = None
	onIndexReached = None
	dll = None
