# Battito In Acciaio

Turns the Carciofo into a percussive synth voice — a stack of six morphing oscillators through a wavefolder and a snappy envelope, fired by a trigger. It draws on the ideas of the classic six-operator drum synths: a continuous waveform morph, a spread from harmonic to inharmonic, and a fold that grows teeth as it is pushed.

## Description

A trigger on CV1 fires the voice; its height sets the velocity. Six oscillators are tuned to a harmonic series that Spread bends toward the inharmonic, each one Morphing continuously from sine through triangle and saw to square. Harmonics decides how many modes sound and how long the upper ones ring — from a single tone, to a fast modal blip over the body, to a long ringing stack. The sum is driven into a folder and shaped by an attack/decay envelope, and the envelope is laid back over the folded signal so a heavily folded hit keeps its punch. CV2 is a pitch input, summed with the Pitch knob a couple of octaves either way.

Three modes, stepped with B1:

| Mode | Sound |
|------|-------|
| Skin | Six oscillators added up — round, tonal bodies |
| Liquid | The same, with a pitch envelope that snaps the attack down — kicks and toms |
| Metal | Two three-operator FM stacks — clangy, alien, noisy |

## Controls

Two layers. Hold B2 to reach the second one; a knob only takes hold once it is turned back through its stored value, so nothing jumps.

| Control | Default layer | Hold B2 |
|---------|---------------|---------|
| P1 | Pitch | Spread |
| P2 | Morph | Harmonics |
| P3 | Fold | Attack (noise · pop · slow) |
| P4 | Decay | Noise |
| CV1 | Trigger in | |
| CV2 | Pitch (1V/oct-ish) | |
| B1 | Step mode | |
| B2 | Hold for the second layer | |

Pitch sets the fundamental. Morph sweeps the waveform. Fold drives the wavefolder, and past three quarters it mixes in a pulse train for extra bite. Decay sets the length of the body. Spread bends the overtones off the harmonic series toward metallic. Harmonics stages the upper modes in — first their ring time, then their level — so it travels from one clean tone to a dense ringing stack. Attack is one control across its travel: left of centre throws a noise transient into the hit, centre is a sharp analog pop, right of centre slows the attack. Noise mixes a steady burst under the body. A harder hit folds and brightens more, not just louder.

Stepping the mode or holding B2 fires the voice once, so you always hear the change.

## LED

- **On power up** it sweeps red, green, blue, white, so you know the firmware is running.
- **Each hit** flashes in the mode's colour — Skin amber, Liquid blue, Metal magenta — at velocity brightness.
- **While you step the mode or work the second layer** it holds that colour steady, washed lighter for the second layer.
- **At rest** it breathes dimly in the mode's colour.
