# Grani

Grani turns the Carciofo into a granular texture processor built on Beads by Émilie Gillet (Mutable Instruments). It records the incoming stereo audio and plays it back as a cloud of short, enveloped grains, transposed, reversed, scattered in time and reverberated, or as a tape delay. It needs audio at its inputs: on its own it is silent. Grani is Italian for grains, and for the beads on a string.

## First minute

Patch a sound into In L (and In R for stereo) and take Out L and Out R. Set P1 (time) around 9 o'clock, P2 (size) around 1 o'clock, P3 (pitch) at noon and turn P4 (density) to 2 o'clock. You hear the dry sound with grains of it scattered on top. Now turn P4 back to noon: the grains stop. **Density at noon means no grains**, so if Grani seems to do nothing, turn P4 away from noon.

## Controls

Every pot has three layers. The main layer plays the grains. Hold B2 for the mix and hold B1 for the random amounts, and each random amount sits under the parameter it scatters. Switching layers never jumps: a pot leaves the stored value alone until you turn it back through it, the LED blinks while it is still being hunted for and flashes white when it catches.

| Pot | Main | Hold B2 | Hold B1 |
|-----|------|---------|---------|
| P1 | Time | Feedback | Time random |
| P2 | Size | Dry / wet | Size random |
| P3 | Pitch | Reverb | Pitch random |
| P4 | Density | Shape | Shape random |

| Control | Function |
|---------|----------|
| B1 | Seed. Tap to take the grains over by hand, then each press plays a grain. Hold B1 and tap B2 (or, with density at noon, hold B1 for 4 s) to hand them back to density. In delay mode: tap tempo |
| B2 | Freeze, on the press. Tap again to let go |
| Hold B2, tap B1 | Choose the next quality, applied when B2 is let go |
| Hold both, nudge a pot | Send CV2 to time (P1), size (P2), pitch (P3) or freeze (P4) |
| Hold both 2 s | Learn the input level again |
| CV1 | Seed in: triggers, gates or a clock, 0 to +5 V |
| CV2 | Pitch at roughly 1 V/oct out of the box, 0 to +5 V |
| In L/R | Audio in. With only In L patched, it feeds both sides |
| Out L/R | Audio out |

- **Time** picks where in the recording the grains come from: fully left is what just came in, turning right reaches back up to a few seconds.
- **Size** is the grain length. A third of the way round, about 10 o'clock, grains are shortest; to the right they grow longer up to 4 s, to the left they grow longer and play backwards. Fully right is delay mode, below.
- **Pitch** transposes by up to two octaves either way. It snaps gently to unison and to 1, 3, 4, 7, 12 and 24 semitones.
- **Density** sets how many grains start. At noon none. To the left grains come at a steady rate, faster further out. To the right they come at random, denser further out, up to the rate of a low C.
- **Feedback** sends the output back into the recording, each quality with its own way of keeping it in check.
- **Dry / wet** goes from the input alone to grains alone.
- **Reverb** adds a reverb after everything else.
- **Shape** is the grain envelope, from hard, clicky edges to the left to soft, slow swells to the right.
- **The random knobs** do nothing at noon. To the right every grain gets a random amount, as much as the knob says. To the left most grains stay close and a few land far away. Time random scatters the grains through the recording, pitch random gives the classic shimmer of random octaves and fifths.

## Seed and clock

Out of the box density makes the grains by itself. A tap on B1 takes them over: from then on a grain plays on every press of B1 and on every trigger on CV1. With density at noon that is exactly one grain each; turned away from noon, a held button or a long gate plays a stream of grains at the density rate. Hold B1 and tap B2 to give the grains back to density, or hold B1 alone for 4 s while it is not playing a stream (density at noon).

Patch a clock into CV1 while density is in charge and density turns into a clock divider: to the left of noon it plays on every 2nd, 3rd ... up to every 16th pulse, to the right of noon it plays each pulse with a growing chance, every pulse fully right. At noon a clock plays no grains. CV1 counts as clocked until 30 s pass without a pulse, so a pause in the music does not set off free-running grains.

Like Beads, Grani waits 2.5 ms after a trigger on CV1 before it plays the grain, so the pitch from a sequencer on CV2 has settled. CV1 and CV2 together play Grani like a voice.

## Freeze

B2 freezes the recording the moment you press it: the grains keep playing what is in the buffer and new input is ignored. A tap on B2 lets go again. A long hold on B2 is the mix layer, so it only freezes while you hold it, and holding B2 while frozen keeps it frozen. With CV2 sent to freeze, a gate on CV2 freezes for as long as it is high.

## Delay mode

Turn size fully right and, after a short crossfade, Grani becomes a delay. The LED flashes white when it does. Density then sets the delay time, the whole buffer at noon and shorter toward either side, right of noon with an extra uneven tap. Time picks multiples of that delay, pitch shifts the repeats like a rotating tape head (noon is clean), shape gives the repeats an envelope (keep it fully left for plain echoes) and the random knobs drift the settings slowly. Tap B1 to set the delay time by hand, or clock CV1: the spacing of the presses is the delay time, and holding B1 for the random layer never counts as a tap. Hold B1 and tap B2 to go back to the density knob. Turn size back down to return to grains.

## Qualities

Four recording qualities, each with its own colour. Hold B2 and tap B1 to step through them: the LED shows the next one, and it is applied when you let go of B2. Tap until the current colour comes round again to cancel. A change clears the recording and a tapped delay time, with a short fade, and is not possible while frozen (the LED blinks dark twice).

| Quality | Colour | Recording | Sound |
|---------|--------|-----------|-------|
| Digital | blue | 48 kHz, 16-bit, about 4 s | Clean and full range |
| Vintage digital | teal | 32 kHz, 12-bit, about 8 s | Early sampler grit |
| Clean tape | amber | 24 kHz, 12-bit, about 11 s | Warm, soft-saturating feedback |
| Dirty tape | pink | 24 kHz, 8-bit µ-law, about 16 s | Wow, flutter and dirt |

## Input level

Like Beads, Grani adjusts its input gain to the level of what you patch in, up to +32 dB for quiet sources. It learns from the first sound after power up and listens for five seconds; loud peaks later are always caught. When you patch in something much quieter, hold both buttons for 2 s to learn the level again.

Grani cannot tell which jacks are patched. With signal on In L only, the left feeds both sides; it goes stereo as soon as In R carries something and back to the left feeding both after 2 s of silence on In R.

## Saved settings

The quality, the CV2 destination and the values on both held layers come back after a power cycle, saved two seconds after you last change them. The main pots are read where they stand, the grains always start under density's control and a freeze ends with the power. Hold both buttons while powering up to go back to the defaults: a little reverb, the dry/wet a bit past noon, a touch of time random and the rest at noon.

## LED

The colour is the quality.

- **Density making grains**: a slow, dim breathing.
- **Density at noon**: dim and still, no grains.
- **Grains by hand** (after a tap on B1): dark, with a blip for every grain.
- **Clocked**: a blip on every pulse.
- **Frozen**: bright and steady.
- **Delay mode**: a steady glow, ticking with the tapped or clocked tempo, or dipping dark every second and a half.
- **Holding B2 / B1**: a pale light, steady for the mix layer, breathing for the random layer. While choosing a quality it pulses in that quality's colour.
- **Holding both**: the colour of the CV2 destination, green for time, yellow for size, violet for pitch, red for freeze.
- A white flash confirms an action, two dark blinks refuse one.

At the heaviest settings (digital quality, a lot of dense, long, randomly pitched grains) the processor can run out of time. Grani then quietly holds density back and fades out the grains closest to their end until it catches up, rather than letting the module freeze. The LED inside the module, on the Daisy Seed, lights while it does.

## Recipes

- **Frozen pad**: play a chord, tap B2, size long, density at 2 o'clock, reverb up.
- **Shimmer**: pitch at +12, pitch random a little right, feedback around 9 o'clock, reverb high.
- **Clocked stutter**: a clock into CV1, density left of noon, size short.
- **Reverse smear**: size left of 10 o'clock, time random right.
- **Tape echo**: size fully right, dirty tape, tap tempo on B1.

## Left out

Beads' wavetable synth (it needs 100 KB of waveforms and a way to tell that nothing is patched), saving a frozen recording across power cycles, manual input gain, the gate output and the 8 s mono buffer did not fit or have no place on this panel.

## Credits

- Beads: Émilie Gillet (Mutable Instruments), MIT licensed
- The notes of the Wish port of Beads to the Daisy (Eight4aWish) helped with memory and processor load

Beads' DSP and stmlib are Émilie Gillet's code, taken unchanged from the [eurorack](https://github.com/pichenettes/eurorack) repository in `lib/eurorack`. The build copies them and applies `grani.patch`, which holds the only changes: the codec runs at 48 kHz in dirty tape too, using Beads' own downsampling path, since the Daisy cannot run its codec at 24 kHz, and a small hook lets the firmware fade out grains when the processor runs out of time. To fit the 128 KB of flash, the synth's wavetables are left out and the sine, µ-law and pitch tables are computed at power up (`tables.cpp`) instead of stored. `shim/beads/drivers/codec.h` keeps only the frame type of Beads' codec driver that the gain control needs. Everything else in this folder is written for the Carciofo. Grani is not affiliated with or endorsed by Mutable Instruments.
