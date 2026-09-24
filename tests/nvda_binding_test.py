"""Drive the NVDA add-on without NVDA.

Two halves are testable here.  synthDrivers/_truvoice.py is the binding: the
ctypes signatures, the callback ABI, the background thread, and the order
audio and marks reach the player.  synthDrivers/truvoice.py is the driver,
and while constructing it wants the whole of NVDA's settings stack, its
speak() only touches four attributes, so it can be called against a stand-in.

That matters because speak() is where this add-on's bugs have been.  The
engine's letter-to-sound rules read the last word of an utterance
differently, so an escape placed after the final text turns a lone "a" from
the letter's name into the article -- and NVDA ends a capital with a closing
PitchCommand and an utterance with an index, both after the text.  The
driver has to hold those back, which is what the last section checks.

The NVDA modules both halves import are stubbed with the smallest thing that
behaves like the real one.

Usage: python tests/nvda_binding_test.py   (needs a 64-bit Python, since the
packaged library is the 64-bit one)
"""

import os
import struct
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ADDON = os.path.join(ROOT, "nvda-addon")

#: The engine's command introducer.  Spelled this way so that no string in
#: this file needs a backslash escape.
ESC = chr(27)

failures = 0


def check(ok, what):
	global failures
	print("%-56s %s" % (what, "ok" if ok else "FAIL"))
	if not ok:
		failures += 1


# ---- the NVDA modules the binding touches ---------------------------------


class FakePlayer:
	def __init__(self, **kw):
		self.kw = kw
		self.chunks = []      # (byte offset at feed time, size)
		self.events = []      # ("audio", nbytes) / ("mark", index)
		self.data = b""       # everything fed, so audio can be compared
		self.total = 0
		self.stopped = 0
		self.paused = None

	def feed(self, data, size=None, onDone=None):
		n = len(data) if size is None else size
		self.chunks.append((self.total, n))
		self.events.append(("audio", n))
		self.data += bytes(data[:n])
		self.total += n
		if onDone:
			# The real player runs this when the chunk finishes playing.
			onDone()

	def idle(self):
		pass

	def stop(self):
		self.stopped += 1

	def pause(self, switch):
		self.paused = switch

	def close(self):
		pass


def install_stubs():
	nvwave = types.ModuleType("nvwave")
	nvwave.WavePlayer = FakePlayer
	sys.modules["nvwave"] = nvwave

	logHandler = types.ModuleType("logHandler")

	class _Log:
		def error(self, *a, **k):
			print("   log.error:", a[0] if a else "")

		def debugWarning(self, *a, **k):
			pass

	logHandler.log = _Log()
	sys.modules["logHandler"] = logHandler

	config = types.ModuleType("config")
	config.conf = {"audio": {"outputDevice": "default"}}
	sys.modules["config"] = config


# ---- the NVDA modules the driver imports -----------------------------------


def install_driver_stubs():
	"""Enough of speech and synthDriverHandler to import the driver.

	The prosody commands are given their multiplier directly; the real ones
	work it out from the user's configured percentage, which is not something
	speak() can see or cares about.
	"""
	# NVDA installs gettext's _ as a builtin before importing drivers.
	import builtins
	if not hasattr(builtins, "_"):
		builtins._ = lambda text: text

	speech = types.ModuleType("speech")
	speech.__path__ = []
	commands = types.ModuleType("speech.commands")

	class SynthCommand:
		def __repr__(self):
			return type(self).__name__

	class IndexCommand(SynthCommand):
		def __init__(self, index):
			self.index = index

	class BreakCommand(SynthCommand):
		def __init__(self, time=0):
			self.time = time

	class CharacterModeCommand(SynthCommand):
		def __init__(self, state):
			self.state = state

	class _Prosody(SynthCommand):
		def __init__(self, multiplier=1):
			self.multiplier = multiplier

	class PitchCommand(_Prosody):
		pass

	class RateCommand(_Prosody):
		pass

	class LangChangeCommand(SynthCommand):
		"""One the driver does not support, to check it is merely ignored."""

	for cls in (SynthCommand, IndexCommand, BreakCommand, CharacterModeCommand,
	            PitchCommand, RateCommand, LangChangeCommand):
		setattr(commands, cls.__name__, cls)

	speechTypes = types.ModuleType("speech.types")
	speechTypes.SpeechSequence = list

	handler = types.ModuleType("synthDriverHandler")

	# NVDA's SynthDriver is an AutoPropertyObject: _get_x and _set_x become
	# the property x.  Without that the driver's settings do not exist as
	# attributes, which is exactly how a broken one reaches a release --
	# the settings dialog is what fails, and nothing here used to touch it.
	class _AutoProperty(type):
		def __new__(mcs, name, bases, ns):
			cls = super().__new__(mcs, name, bases, ns)
			names = set()
			for key in ns:
				if key.startswith("_get_") or key.startswith("_set_"):
					names.add(key[5:])
			for n in names:
				setattr(cls, n, property(getattr(cls, "_get_" + n, None),
					                         getattr(cls, "_set_" + n, None)))
			return cls

	class SynthDriver(metaclass=_AutoProperty):
		# The real base bridges these two; a driver supplies _getAvailableX
		# and the property comes from here.  Custom settings get no such
		# bridge, which is the trap this whole section exists for.
		def _get_availableVoices(self):
			return self._getAvailableVoices()

		def _get_availableVariants(self):
			return self._getAvailableVariants()

		@classmethod
		def VoiceSetting(cls):
			return "voice"

		@classmethod
		def RateSetting(cls, minStep=1):
			return "rate"

		@classmethod
		def PitchSetting(cls, minStep=1):
			return "pitch"

		@classmethod
		def VolumeSetting(cls, minStep=1):
			return "volume"

		def __init__(self):
			pass

	class _Notifier:
		def notify(self, **kw):
			pass

	handler.SynthDriver = SynthDriver
	# NVDA's VoiceInfo is a StringParameterInfo with a language on it; the
	# language is what drives automatic voice switching, so the stub has to
	# carry it or the tests cannot see whether the driver sets it.
	class VoiceInfo:
		def __init__(self, id, displayName, language=None):
			self.id, self.displayName = id, displayName
			self.language = language

	handler.VoiceInfo = VoiceInfo
	handler.synthDoneSpeaking = _Notifier()
	handler.synthIndexReached = _Notifier()

	autoSettings = types.ModuleType("autoSettingsUtils")
	autoSettings.__path__ = []
	ds = types.ModuleType("autoSettingsUtils.driverSetting")

	class DriverSetting:
		def __init__(self, id, displayNameWithAccelerator, availableInSettingsRing=False,
			defaultVal=None, displayName=None, useConfig=True):
			self.id = id
			self.defaultVal = defaultVal
			self.displayName = displayName or displayNameWithAccelerator.replace("&", "")

	ds.DriverSetting = DriverSetting
	us = types.ModuleType("autoSettingsUtils.utils")

	class StringParameterInfo:
		def __init__(self, id, displayName):
			self.id, self.displayName = id, displayName

	us.StringParameterInfo = StringParameterInfo
	sys.modules["autoSettingsUtils"] = autoSettings
	sys.modules["autoSettingsUtils.driverSetting"] = ds
	sys.modules["autoSettingsUtils.utils"] = us
	sys.modules["speech"] = speech
	sys.modules["speech.commands"] = commands
	sys.modules["speech.types"] = speechTypes
	sys.modules["synthDriverHandler"] = handler
	return commands


def endsWithEscape(text):
	"""Whether text finishes with an engine command rather than a word."""
	i = text.rfind(ESC)
	if i < 0:
		return False
	for j in range(i + 2, len(text)):
		if text[j].isalpha():
			return j == len(text) - 1
	return True


# ---- the binding -----------------------------------------------------------


def binding_tests(_truvoice):
	marks = []
	done = []

	def onIndex(index):
		(done if index is None else marks).append(index)

	_truvoice.initialize(onIndex)
	player = _truvoice.player
	check(player.kw.get("samplesPerSec") == 11025, "player opened at 11025 Hz")
	check(player.kw.get("channels") == 1 and player.kw.get("bitsPerSample") == 16,
	      "player opened as 16-bit mono")

	check(_truvoice.voiceCount() == 10, "ten voices")
	want = ["Peter", "Sidney", "Eager Eddie", "Deep Douglas", "Biff",
	        "Grandpa Amos", "Melvin", "Alex", "Wanda", "Julia"]
	got = [_truvoice.voiceName(i) for i in range(10)]
	check(got == want, "the voices are named in the engine's own order")
	if got != want:
		print("     got: %s" % ", ".join(got))
	check(_truvoice.voiceRate(0) == 150 and _truvoice.voiceRate(5) == 120,
	      "the elderly voice has its own slower default")

	# --- a plain utterance ---
	_truvoice.speak("Hello world.")
	_truvoice.bgQueue.join()
	check(player.total > 0, "speaking produced audio")
	check(len(done) == 1, "one done-speaking notification")

	# --- marks, and where they land ---
	before = player.total
	marks.clear()
	done.clear()
	text = (_truvoice.markSequence(11) + "Alpha "
	        + _truvoice.markSequence(22) + "beta "
	        + _truvoice.markSequence(33) + "gamma.")
	audio_at_mark = []
	orig_feed = player.feed

	def spy(data, size=None, onDone=None):
		if onDone:
			audio_at_mark.append(player.total + (len(data) if size is None else size))
		orig_feed(data, size, onDone)

	player.feed = spy
	_truvoice.speak(text)
	_truvoice.bgQueue.join()
	player.feed = orig_feed
	check(marks == [11, 22, 33], "marks arrive in order with their own values")
	check(len(done) == 1, "the utterance still reports done")
	check(all(a <= player.total for a in audio_at_mark),
	      "each mark fires after the audio that precedes it")
	check(player.total > before, "the marked utterance produced audio")

	# --- cancelling ---
	marks.clear()
	done.clear()
	long_text = "This is a considerably longer sentence, " * 8
	_truvoice.speak(long_text)
	_truvoice.stop()
	_truvoice.bgQueue.join()
	check(player.stopped >= 1, "cancelling stops the player")

	# and the synth still works afterwards
	after = player.total
	_truvoice.speak("Still here.")
	_truvoice.bgQueue.join()
	check(player.total > after, "the synth still speaks after a cancel")

	_truvoice.pause(True)
	check(player.paused is True, "pause reaches the player")
	_truvoice.pause(False)

	# --- the escapes the driver builds ---
	check(_truvoice.markSequence(42) == ESC + "[42i", "mark escape")
	check(_truvoice.breakSequence(0) == ESC + "[49s", "a zero break is silence")
	check(_truvoice.breakSequence(500) == ESC + "[99s", "500 ms break")
	check(_truvoice.breakSequence(60000) == ESC + "[255s", "a long break is clamped")
	check(_truvoice.punctuationSequence(True) == ESC + "[2N", "punctuation on")
	check(_truvoice.punctuationSequence(False) == ESC + "[2F", "punctuation off")
	check(_truvoice.pitchSequence(170) == ESC + "[85p", "pitch escape is half the raw value")
	check(_truvoice.pitchSequence(10) == ESC + "[25p", "a low pitch is clamped")
	check(_truvoice.pitchSequence(9999) == ESC + "[200p", "a high pitch is clamped")
	check(_truvoice.rateSequence(150) == ESC + "[150r", "rate escape")
	check(_truvoice.rateSequence(10) == ESC + "[46r",
		"a low rate is clamped to the first table row")
	check(_truvoice.rateSequence(9999) == ESC + "[400r",
		"a high rate is clamped to the last row OpenTV adds")

	def audioOf(text, **kw):
		start = len(player.data)
		_truvoice.speak(text, **kw)
		_truvoice.bgQueue.join()
		return player.data[start:]

	# --- output rate ---
	# The engine has three, each with its own resonator tables.  16 kHz is
	# OpenTV's: the original only ever shipped 8 kHz and 11.025.
	check(_truvoice.sampleRateHz(0) == 8000 and _truvoice.sampleRateHz(1) == 11025
		and _truvoice.sampleRateHz(2) == 16000, "three output rates")
	check(_truvoice.getSampleRate() == 1, "11 kHz is the default")
	before = len(player.data)
	check(_truvoice.setSampleRate(2), "switching to 16 kHz is accepted")
	check(_truvoice.getSampleRate() == 2, "and takes effect")
	check(_truvoice.player.kw.get("samplesPerSec") == 16000,
		"the player is reopened at the new rate")
	_truvoice.speak("Hello world.")
	_truvoice.bgQueue.join()
	check(len(_truvoice.player.data) > 0, "and it still speaks")
	check(_truvoice.setSampleRate(1), "switching back is accepted")
	check(_truvoice.player.kw.get("samplesPerSec") == 11025, "player follows back")
	player = _truvoice.player

	# --- the rate floor ---
	# Engine_SetSpeed does (wpm - 46) >> 3 unsigned, so below 46 the index
	# wraps to about 0x1fffffff and the engine reads wildly.  The original
	# crashes there too, so the setter floors it -- if it did not, this
	# would take the test process down rather than fail.
	_truvoice.setRate(1)
	_truvoice.bgQueue.join()
	check(len(audioOf("Hello world.")) > 0, "an absurdly low rate does not crash")
	_truvoice.setRate(_truvoice.voiceRate(0))
	_truvoice.bgQueue.join()

	# --- what the letter bug actually is ---
	# The engine names a letter given on its own.  It stops doing so as soon
	# as anything follows, which is why the driver may not close an utterance
	# with an escape.  Both halves of that are worth pinning down.
	plain = audioOf("A")
	check(len(plain) > 0, "a lone letter speaks")
	check(audioOf(ESC + "[42i" + "A") == plain,
	      "an escape before the letter is harmless")
	check(audioOf("A" + ESC + "[42i") != plain,
	      "an escape after the letter changes how it reads")

	# --- so a trailing mark is reported rather than sent ---
	marks.clear()
	done.clear()
	check(audioOf("A", trailingMarks=[42]) == plain,
	      "a held mark leaves the letter alone")
	check(marks == [42], "a held mark is still reported")
	check(len(done) == 1, "and the utterance still reports done")

	# --- an inline prosody escape outlives its utterance ---
	pitch0, rate0 = _truvoice.voicePitch(0), _truvoice.voiceRate(0)
	base = audioOf("Hello world.")
	raised = audioOf(ESC + "[150p" + "Hello world.")
	check(raised != base, "an inline pitch escape changes the audio")
	check(audioOf("Hello world.") == raised,
	      "and stays in effect for the next utterance")
	audioOf(ESC + "[150p" + "Hello world.", restore=(pitch0, rate0))
	check(audioOf("Hello world.") == base,
	      "the restore the driver passes puts the pitch back exactly")
	audioOf(ESC + "[250r" + "Hello world.", restore=(pitch0, rate0))
	check(audioOf("Hello world.") == base, "and the rate with it")


# ---- the driver ------------------------------------------------------------


def driver_tests(_truvoice, commands):
	from synthDrivers import truvoice

	sent = []
	fired = []
	real_speak = _truvoice.speak

	def fake_speak(text, trailingMarks=(), restore=None):
		sent.append((text, list(trailingMarks), restore))

	_truvoice.speak = fake_speak

	# speak() reads only these four, so the class need not be constructed --
	# that would want NVDA's whole settings stack.
	driver = types.SimpleNamespace(
		_enginePitch=85,
		_engineRate=150,
		_escapeText=truvoice.SynthDriver._escapeText,
		_onIndexReached=fired.append,
	)

	def say(sequence):
		sent.clear()
		fired.clear()
		truvoice.SynthDriver.speak(driver, sequence)
		return sent[0] if sent else None

	# Pitch is absolute: picking a voice must move the slider to where that
	# voice sits, rather than leaving it reading 50% for all ten.
	pitches = [_truvoice.voicePitch(i) for i in range(10)]
	percents = [truvoice._pitchToPercent(v) for v in pitches]
	check(len(set(percents)) == len(set(pitches)),
		"each voice's pitch maps to its own slider position")
	check(max(percents) - min(percents) > 50,
		"and they spread across it rather than bunching")
	# The slider covers the engine, not just the voices: everything above
	# Wanda is pitch the engine can reach and no stock voice uses.
	check(max(percents) < 75,
		"the slider keeps headroom above the highest voice")
	check(truvoice._pitchFromPercent(100) > max(pitches) * 2,
		"and that headroom is worth having")
	check([p for _, p in sorted(zip(pitches, percents))] == sorted(percents),
		"a higher pitch always reads as a higher position")
	# The round trip only has to be close -- the slider is 101 steps over a
	# 210-unit range -- but a voice left alone must keep its exact pitch.
	# The slider is 101 steps over a ten-fold range, so one step is about
	# 2.3% -- a few units at the top.  A voice picked and left alone still
	# gets its exact pitch; this only bounds what moving the slider costs.
	check(all(abs(truvoice._pitchFromPercent(truvoice._pitchToPercent(v)) - v)
			<= max(2, v // 20) for v in pitches),
		"percent and pitch round-trip within a step")
	# 50..500 is what stage 2 clamps to, and 500 is the largest pitch
	# whose half still fits the byte it is stored in.
	check(truvoice._pitchFromPercent(0) == 50,
		"the bottom of the slider is the engine's lowest pitch")
	check(truvoice._pitchFromPercent(100) == 500,
		"and the top is its highest")

	# --- every declared setting must actually resolve --------------------
	# The settings dialog walks supportedSettings and, for each string
	# setting, reads available<Id>s off the synth -- note the capitalize(),
	# which lowercases the rest of the id.  A setting whose accessors are
	# named even slightly wrong raises there, and the whole dialog fails to
	# open.  That shipped once; this is here so it cannot again.
	real_init = _truvoice.initialize
	_truvoice.initialize = lambda cb: None
	try:
		synth = truvoice.SynthDriver()
	finally:
		_truvoice.initialize = real_init
	for setting in truvoice.SynthDriver.supportedSettings:
		sid = getattr(setting, "id", setting)
		if not isinstance(sid, str):
			continue
		check(hasattr(synth, sid),
			"setting %r is readable as an attribute" % sid)
		# Only the string settings carry a choice list.
		if type(setting).__name__ == "DriverSetting":
			attr = "available%ss" % sid.capitalize()
			choices = getattr(synth, attr, None)
			check(choices is not None, "%s exists for setting %r" % (attr, sid))
			check(choices is not None and len(list(choices.values())) > 0,
				"%s offers at least one choice" % attr)
			# And the dialog round-trips the value through the property.
			if choices:
				first = list(choices.keys())[0]
				setattr(synth, sid, first)
				check(str(getattr(synth, sid)) == str(first),
					"setting %r round-trips through its property" % sid)

	# --- voice ids carry their language -----------------------------------
	# TruVoice shipped five languages and only English is decompiled, but the
	# ids are prefixed now so that adding one later does not renumber anyone's
	# saved voice, and so NVDA can pick a voice by language.
	voices = synth.availableVoices
	check(list(voices) == ["en:%d" % i for i in range(10)],
		"voices are keyed language:index")
	check(all(v.language == "en" for v in voices.values()),
		"and each carries its language for automatic switching")
	check(voices["en:8"].displayName == "Wanda", "with the right names")

	# A setting saved before the prefix existed is a bare index and means
	# English; migrating it keeps people on the voice they chose.
	synth.voice = "3"
	check(synth.voice == "en:3", "a bare saved index migrates to English")
	check(synth._voiceRate() == _truvoice.voiceRate(3),
		"and really selects that voice")
	synth.voice = "en:5"
	check(synth.voice == "en:5", "a prefixed id is taken as it is")
	synth.voice = "zz:99"
	check(synth.voice == "en:0", "an unknown voice falls back to the first")

	# The rate slider used to reach 400 wpm, well past the 26th and last row
	# of the engine's rate table, so its top third made speech slower and
	# stranger rather than faster.
	rates = [truvoice._fromPercent(pct, truvoice.MIN_WPM, truvoice.MAX_WPM, 150)
		for pct in range(101)]
	check(all(46 <= wpm <= 400 for wpm in rates),
		"every rate slider position lands inside the rate table")
	check(rates == sorted(rates) and rates[0] == 46 and rates[-1] == 400,
		"the slider rises across the whole of that range")
	# 26 rows are the original's; OpenTV adds rows 26..44 on top, and the
	# slider should reach every one of them.
	check(len({(wpm - 46) >> 3 for wpm in rates}) == 45,
		"and reaches all 45 rows, the original 26 plus the added 19")

	# The reported bug: changing voice left the slider reading 50%.
	driver._voice = "0"
	moved = []
	for i in (0, 1, 8):
		pitch = _truvoice.voicePitch(i)
		moved.append(truvoice._pitchToPercent(pitch))
	check(len(set(moved)) == 3,
		"Peter, Sidney and Wanda each report a different slider position")
	check(moved[1] < moved[0] < moved[2],
		"lowest voice lowest, highest voice highest")

	check(commands.CharacterModeCommand not in truvoice.SynthDriver.supportedCommands,
	      "the driver no longer claims character mode")

	# The sequence NVDA sends for a typed capital, verbatim in shape:
	# raise the pitch, the letter, put the pitch back.
	text, trailing, restore = say([
		commands.PitchCommand(multiplier=1.6),
		"A",
		commands.PitchCommand(),
	])
	check(text == ESC + "[68p" + "A", "a capital is raised in pitch before the letter")
	check(not endsWithEscape(text), "and nothing follows the letter")
	check(restore == (85, 150), "the pitch is put back through the setters instead")

	# NVDA ends an utterance with an index.
	text, trailing, restore = say([commands.IndexCommand(1), "A", commands.IndexCommand(2)])
	check(text == ESC + "[1i" + "A", "an index before the text is sent as an escape")
	check(trailing == [2], "an index after the text is held back")
	check(not endsWithEscape(text), "so the utterance still ends in the letter")

	# An index between two pieces of text is not trailing at all.
	text, trailing, restore = say(["one ", commands.IndexCommand(5), "two"])
	check(text == "one " + ESC + "[5i" + "two" and trailing == [],
	      "an index between words stays where it was")

	# Character mode is accepted and ignored.
	text, trailing, restore = say([
		commands.CharacterModeCommand(True),
		"A",
		commands.CharacterModeCommand(False),
	])
	check(text == "A", "character mode adds nothing either side of the letter")

	# A break after the last word has nothing left to apply to.
	text, trailing, restore = say(["Hi.", commands.BreakCommand(500)])
	check(text == "Hi." and trailing == [], "a trailing break is dropped")
	text, trailing, restore = say([commands.BreakCommand(500), "Hi."])
	check(text == ESC + "[99s" + "Hi.", "a leading break is kept")

	# An unsupported command is ignored rather than fatal.
	text, trailing, restore = say([commands.LangChangeCommand(), "Hi."])
	check(text == "Hi.", "an unsupported command is ignored")

	# Nothing to say, but the notifications are still owed.
	check(say([commands.IndexCommand(9)]) is None, "an index alone speaks nothing")
	check(fired == [9, None], "and its mark and end are reported anyway")

	# ESC in the text would be read as a command.
	text, trailing, restore = say(["a" + ESC + "[2Nb"])
	check(ESC not in text, "an escape in the text is neutralised")

	_truvoice.speak = real_speak


def main():
	global failures
	if struct.calcsize("P") != 8:
		sys.exit("needs a 64-bit Python: the packaged library is 64-bit")
	install_stubs()
	commands = install_driver_stubs()
	sys.path.insert(0, ADDON)
	from synthDrivers import _truvoice

	check(_truvoice.isAvailable(), "the library is where the driver expects it")
	binding_tests(_truvoice)
	print()
	driver_tests(_truvoice, commands)

	_truvoice.terminate()
	check(_truvoice.synth is None, "terminate tears the synth down")

	print("all passed" if not failures else "FAILED")
	return 1 if failures else 0


if __name__ == "__main__":
	sys.exit(main())
