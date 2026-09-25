# The speak window

`build/bin/speakwin.exe` is a window for typing text and hearing the
engine say it: pick a voice, move the rate, pitch and volume, choose a
sample rate, and save the result as a WAV file.

It needs `tvtts64.dll` beside it, which `harness/build.sh` puts there.
Nothing else -- no Centigram binary, no SAPI, no registry entries.

## Keys

| | |
|---|---|
| F5 | Speak |
| F6 | Pause, and again to resume |
| F7 | Stop |
| F8 | Reset to defaults; the engine says "Reset speech" |
| Ctrl+S | Export to WAV |
| Escape | Stop |
| Enter | Speak, unless the text box has focus, where it starts a new line |
| Tab, Shift+Tab | Move between controls |
| Alt+letter | Jump to a control by its underlined letter |

Every button also carries its key in its label, so a screen reader reads
"Speak F5 button" and the shortcut is discoverable without the
documentation.

## Why plain Win32

Screen reader support was the first requirement, and the shortest path to it
is to use the controls Windows already ships. A stock edit box, combo box,
trackbar and button each carry their own MSAA implementation, so NVDA, JAWS
and Narrator read them correctly without this program doing anything at all.
A toolkit that draws its own controls has to reimplement that, and generally
reimplements some of it wrong; drawing them by hand means writing an
`IAccessible` from scratch.

So the window is plain Win32 in C, which also means it has no dependency to
install and builds with the toolchain the repository already uses.

Four rules keep it that way, and they are the parts worth not breaking:

* **Every control is a stock control.** No owner-draw, no custom classes.

* **Every control is created immediately after the static that labels it.**
  That adjacency in z-order is what MSAA uses to give a control its
  accessible name -- it is not a property you set, it is the previous
  sibling's text. Put anything between a label and its control and the
  control loses its name. The five buttons need no label because a button's
  name is its own text.

* **The window is an actual dialog**, created from a template with no
  controls in it -- the children are made in `WM_INITDIALOG` as they would
  have been in `WM_CREATE`. That is what makes Tab, Shift+Tab, the arrow
  keys and Alt+mnemonics navigate, and it is what a screen reader reads as a
  dialog rather than as a bare window.

  The first version was a plain window with `WS_EX_CONTROLPARENT` and
  `IsDialogMessage` in the loop, which is the usual advice, and Tab did not
  work at all. The loop was not the problem; the initial focus was. A
  `SetFocus` in `WM_CREATE` runs before the window is visible, the
  activation that follows puts focus back on the frame, and with focus on
  the frame rather than on a child there is nothing for Tab to move on
  from. A dialog settles it by owning that moment: returning TRUE from
  `WM_INITDIALOG` puts focus on the first control with `WS_TABSTOP`.
  `IsDialogMessage` is still in the loop, because a modeless dialog needs
  it.

* **The font comes from `SPI_GETNONCLIENTMETRICS`** and the colours are
  system colours, so the window follows whatever size and high-contrast
  theme the user has set rather than a hard-coded pair.

The window opens centred on the **work area** of whichever monitor it lands
on, so it neither sits under the taskbar nor assumes a single screen. The
centring happens after the size is set, because centring a window before
resizing it centres the wrong rectangle.

The layout is computed from that font's own character cell, so a larger
interface font gives a larger window rather than clipped text.

## How it plays

Synthesis runs on a worker thread and hands samples to `waveOut` as they are
produced rather than after the whole text is done, so speaking starts at
once and a long text does not freeze the window. Pause and stop are
`waveOutPause` and `waveOutReset`, which is why pause resumes exactly where
it left off instead of restarting the sentence.

There are eight buffers of 2048 samples, and the producer waits for the one
it is about to fill rather than the one it is about to submit. That
distinction is the whole thing: the engine synthesises far faster than real
time, so waiting only at submission lets it lap the device after eight
blocks and rewrite the samples the card is reading -- which sounds like a
glitch every second and a half rather than like a crash, and so is the kind
of bug that survives a casual listen.

The engine object is created once and touched only from the worker thread.
The window collects settings into plain variables and the worker applies
them just before it speaks, so there is no lock around the synth and no
question of two threads being in it at once -- which matters, because
`tvtts.h` says a `tvtts_synth` is not thread safe.

Exporting takes the same path with the audio going to a buffer instead of
the device, and writes a mono 16-bit RIFF file at whichever sample rate is
selected.

## Changing voice changes rate and pitch

The engine keeps a default rate and pitch for each voice -- Wanda's pitch is
50 where Peter's is 85 -- and picking a voice adopts them, which is what
SAPI did and what makes each voice sound like itself rather than like the
last one at the wrong pitch. The sliders move and the status line says so,
so it is visible rather than silent:

    Wanda: rate 150, pitch 50.

F8 restores voice 0 with its own defaults, full volume and 11 kHz, and then
has the engine say "Reset speech" -- the announcement is the engine's own
voice, which is the feedback that suits a speech program.

## What the sliders reach

Rate runs from 46 to 400 words per minute and pitch from 50 to 500 Hz, which
are the ranges `tvtts.h` documents. Above 253 wpm the rate extension is what
is speaking: the original engine's table stops there, and OpenTV shortens
durations beyond it instead. Volume is a percentage of the engine's
0..0xffff, and below about 0.1% the engine mutes rather than fading, which
is the original's behaviour.
