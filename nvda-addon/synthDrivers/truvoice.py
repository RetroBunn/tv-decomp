# TruVoice for NVDA.
# This file is covered by the GNU General Public License, version 2 or later.

"""Centigram TruVoice, as a native NVDA synthesizer.

The engine is a decompilation of the 1997 SAPI 4 original, built as an
ordinary DLL, so none of SAPI is in the way: no COM, no registry, no bridge
process.  See the OpenTV project for what that means and how it is
verified.

Rate, pitch and volume are the percentages NVDA hands every driver; the
engine wants words per minute and its own pitch scale, so they are mapped
here.  The two sliders do not work the same way.

Rate is relative: fifty percent is the voice's own default, so every voice
speaks at its intended speed with the slider centred.  Nine of the ten
share a default of 150 wpm anyway.

Pitch is absolute: the slider is one scale for all ten voices, so picking
a voice moves it to wherever that voice sits -- Sidney at the very bottom,
Wanda at 62%.  It is logarithmic, because pitch is heard
in ratios rather than in steps, which is what spreads the ten out evenly
instead of bunching the six male voices into the bottom quarter.
"""

import math

from collections import OrderedDict

from logHandler import log
from speech.commands import (
	BreakCommand,
	CharacterModeCommand,
	IndexCommand,
	PitchCommand,
	RateCommand,
)
from speech.types import SpeechSequence
from synthDriverHandler import (
	SynthDriver,
	VoiceInfo,
	synthDoneSpeaking,
	synthIndexReached,
)

from . import _truvoice

#: Words per minute at 0% and 100%.  The engine's own rate table has 26
#: rows, 46..253 wpm picked by (wpm - 46) // 8, and above them it read
#: off the end -- a sentence at 254 came out ten times longer than at
#: 253.  OpenTV adds rows past that which shorten durations instead, so
#: 254..400 now genuinely speeds up, to about three and a half times the
#: speed of 150 wpm.  Everything at 253 and below is bit-for-bit the
#: 1997 engine, so the voices still sound the way people know them.
MIN_WPM, MAX_WPM = 46, 400
#: The engine's full pitch range, in the units its voice table uses.
#: Stage 2 clamps every node to 50..500 and stores the value halved in a
#: byte, so that is exactly what the engine can represent -- 500 is the
#: largest pitch whose half still fits.  The ten voices sit inside it,
#: Sidney lowest at 50 and Wanda highest at 208, which leaves most of the
#: top half of the slider above any stock voice.
MIN_PITCH, MAX_PITCH = 50, 500


def _fromPercent(percent: int, low: int, high: int, default: int) -> int:
	"""Map 0-100 onto low..high with 50 landing on `default`."""
	percent = max(0, min(100, percent))
	if percent < 50:
		return int(round(low + (default - low) * (percent / 50.0)))
	return int(round(default + (high - default) * ((percent - 50) / 50.0)))


def _pitchFromPercent(percent: int) -> int:
	"""Slider position to engine pitch, logarithmically."""
	percent = max(0, min(100, percent))
	lo, hi = math.log(MIN_PITCH), math.log(MAX_PITCH)
	return int(round(math.exp(lo + (hi - lo) * (percent / 100.0))))


def _pitchToPercent(pitch: int) -> int:
	"""Engine pitch back to a slider position."""
	pitch = max(MIN_PITCH, min(MAX_PITCH, pitch))
	lo, hi = math.log(MIN_PITCH), math.log(MAX_PITCH)
	return int(round(100.0 * (math.log(pitch) - lo) / (hi - lo)))


class SynthDriver(SynthDriver):
	name = "truvoice"
	description = "Centigram TruVoice"

	supportedSettings = (
		SynthDriver.VoiceSetting(),
		SynthDriver.RateSetting(),
		SynthDriver.PitchSetting(),
		SynthDriver.VolumeSetting(),
	)
	supportedCommands = {  # noqa: RUF012
		IndexCommand,
		BreakCommand,
		# NVDA raises the pitch to mark a capital.
		PitchCommand,
		RateCommand,
		# CharacterModeCommand is deliberately absent: the engine already
		# names a letter correctly when the letter is all it is given, and
		# the flag it would map to is speak-punctuation, not spell mode.
	}
	supportedNotifications = {synthIndexReached, synthDoneSpeaking}  # noqa: RUF012

	@classmethod
	def check(cls) -> bool:
		return _truvoice.isAvailable()

	def __init__(self):
		super().__init__()
		_truvoice.initialize(self._onIndexReached)
		self._voice = "0"
		self._rate = 50
		self._volume = 100
		# The engine's own units, kept so a PitchCommand or RateCommand can
		# scale from where the user actually is rather than from a default.
		self._engineRate = _truvoice.voiceRate(0)
		self._enginePitch = _truvoice.voicePitch(0)
		# Pitch is absolute, so the slider reads wherever the voice sits;
		# setting the voice below overwrites both of these.
		self._pitch = _pitchToPercent(self._enginePitch)
		# Apply them so the engine and the driver agree from the start.
		self.voice = self._voice
		self.volume = self._volume

	def terminate(self):
		_truvoice.terminate()

	# ---- speaking ---------------------------------------------------------

	def speak(self, speechSequence: SpeechSequence):
		"""Say a sequence.

		An engine escape placed after the last text changes how that text
		is read: the letter-to-sound rules no longer see the final word as
		final, so a lone "a" comes out as the article rather than the
		letter's name.  NVDA ends a capital with a closing PitchCommand and
		an utterance with an index, both after the text, which is exactly
		that case -- so anything that would follow the last text is held
		back here.  Marks among it are delivered when the utterance ends
		and the rest is dropped, having nothing left to apply to.
		"""
		parts: list[str] = []
		#: Escapes seen since the last text, with the mark each carries.
		held: list[tuple[str, int | None]] = []
		for item in speechSequence:
			if isinstance(item, str):
				text = self._escapeText(item)
				if not text:
					continue
				# Text follows, so the held escapes are no longer trailing.
				parts.extend(escape for escape, _ in held)
				held.clear()
				parts.append(text)
			elif isinstance(item, IndexCommand):
				held.append((_truvoice.markSequence(item.index), item.index))
			elif isinstance(item, BreakCommand):
				held.append((_truvoice.breakSequence(item.time), None))
			elif isinstance(item, PitchCommand):
				held.append((_truvoice.pitchSequence(
					int(round(self._enginePitch * item.multiplier)),
				), None))
			elif isinstance(item, RateCommand):
				held.append((_truvoice.rateSequence(
					int(round(self._engineRate * item.multiplier)),
				), None))
			elif isinstance(item, CharacterModeCommand):
				# Nothing to do: a letter on its own already says its name.
				pass
			else:
				log.debugWarning(f"Unsupported speech command: {item}")
		# Marks that ended up trailing cannot be sent as escapes, so the
		# library reports them once the audio before them has played.
		trailing = [index for _, index in held if index is not None]
		text = "".join(parts)
		if text:
			# An inline pitch or rate escape stays in effect for every later
			# utterance on this synth, and cannot be closed with a trailing
			# escape, so the setters put both back once the text is spoken.
			_truvoice.speak(text, trailing, (self._enginePitch, self._engineRate))
		else:
			# Nothing to say, but the caller is still owed the notifications.
			for index in trailing:
				self._onIndexReached(index)
			self._onIndexReached(None)

	@staticmethod
	def _escapeText(text: str) -> str:
		"""ESC starts an engine command, so text may not contain one."""
		return text.replace("\x1b", " ")

	def cancel(self):
		_truvoice.stop()

	def pause(self, switch: bool):
		_truvoice.pause(switch)

	def _onIndexReached(self, index: int | None):
		if index is not None:
			synthIndexReached.notify(synth=self, index=index)
		else:
			synthDoneSpeaking.notify(synth=self)

	# ---- voices -----------------------------------------------------------

	def _getAvailableVoices(self) -> OrderedDict[str, VoiceInfo]:
		voices = OrderedDict()
		for i in range(_truvoice.voiceCount()):
			voices[str(i)] = VoiceInfo(str(i), _truvoice.voiceName(i), "en")
		return voices

	def _get_voice(self) -> str:
		return self._voice

	def _set_voice(self, value: str):
		if value not in self.availableVoices:
			value = "0"
		self._voice = value
		_truvoice.setVoice(int(value))
		# Rate is relative to the voice's own default, so re-apply the
		# percentage against the new default rather than the old one.
		self.rate = self._rate
		# Pitch is absolute, so the voice brings its own and the slider
		# follows it.  The engine value is taken from the table rather
		# than back through the percentage, which would round it: a voice
		# picked and left alone sounds exactly as it should.
		self._enginePitch = _truvoice.voicePitch(int(value))
		self._pitch = _pitchToPercent(self._enginePitch)
		_truvoice.setPitch(self._enginePitch)

	def _voiceRate(self) -> int:
		"""The current voice's own words per minute, which 50% maps to."""
		return _truvoice.voiceRate(int(self._voice))

	# ---- rate, pitch, volume ---------------------------------------------

	def _get_rate(self) -> int:
		return self._rate

	def _set_rate(self, percent: int):
		self._rate = max(0, min(100, percent))
		wpm = self._voiceRate()
		self._engineRate = _fromPercent(self._rate, MIN_WPM, MAX_WPM, wpm)
		_truvoice.setRate(self._engineRate)

	def _get_pitch(self) -> int:
		return self._pitch

	def _set_pitch(self, percent: int):
		self._pitch = max(0, min(100, percent))
		self._enginePitch = _pitchFromPercent(self._pitch)
		_truvoice.setPitch(self._enginePitch)

	def _get_volume(self) -> int:
		return self._volume

	def _set_volume(self, percent: int):
		self._volume = max(0, min(100, percent))
		_truvoice.setVolume(int(self._volume * 0xFFFF / 100))
