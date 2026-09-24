# Nembo

Nembo turns the Carciofo into Clouds, the texture synthesizer by Émilie Gillet (Mutable Instruments), through Ben Sergentanis' Daisy port (Nimbus). It records the incoming stereo audio into a short buffer and plays back overlapping, enveloped fragments of it, transposed, diffused and reverberated.

On the Löwenzahnhonig this lived as one build per Clouds mode, because the module had no way to switch anything at runtime. The Carciofo has two buttons and an RGB LED, so Nembo folds every mode, the fidelity switch and freeze into a single firmware, and adds a second pot layer for Clouds' blend controls.

## Modes

Press B1 to step through Clouds' four playback modes. The LED colour follows the mode.

| Mode | Colour | Sound |
|------|--------|-------|
| Granular | green | Overlapping enveloped grains — the classic cloud, a pitch shifter, a thickener |
| Stretch | cyan | Overlapping windows: clean transposition and time-stretching of a frozen buffer |
| Looping delay | blue | A buffer looped and pitched; P2 is feedback, P4 the loop length |
| Spectral | magenta | Phase-vocoder smear, drones and metallic wash |

Hold B1 to toggle the buffer between hi-fi (16-bit stereo, ~0.7 s) and lo-fi (8-bit µ-law mono, ~5 s of grittier audio). The LED washes out a little in lo-fi.

## Controls

The output is fully wet by default and always soft-clipped, as on Clouds. Hold B2 to reach the second pot layer; a pot only takes hold once turned back through its stored value, so switching layers never jumps.

| Control | Default layer | Hold B2 |
|---------|---------------|---------|
| P1 | Position | Dry / wet |
| P2 | Size, or feedback in looping delay | Reverb |
| P3 | Pitch (±2 octaves, unison dead zone) | Stereo spread |
| P4 | Density, texture or size, per mode | Texture, or feedback where P4 is texture |
| CV1 | Position, added to P1 | |
| CV2 | Freeze while high, one grain per rising edge | |
| B1 | Step mode; hold to toggle hi-fi / lo-fi | |
| B2 | Tap to freeze; hold for the second layer | |
| In L/R | Audio in (recorded while not frozen) | |
| Out L/R | Audio out (fully wet, soft-clipped) | |

Position is where in the buffer the grains are taken from — fully counter-clockwise is the most recent audio, clockwise travels back in time. Pitch has a small unison zone around the centre so a roughly centred pot is exactly unison. Tap B2 or raise CV2 to freeze the buffer and play the captured audio with the pots; each rising edge on CV2 also fires a single grain, so a clock into CV2 with density low turns the module into a trigger-driven micro-sampler.

## LED

- **Each mode** has its own colour; **lo-fi** desaturates it.
- **Frozen** (tap B2 or a high gate on CV2): the colour pulses brightly.
- **Second layer** (holding B2): a soft, washed-out light.
- **Live**: a dim breathing glow in the mode's colour. Changing mode, fidelity or freeze flashes once.

## Credits

- Clouds: Émilie Gillet (Mutable Instruments), MIT licensed
- Nimbus (Daisy port of Clouds): Ben Sergentanis (Electro-Smith)
- Löwenzahnhonig Nimbus, which this is based on: Ben van der Burgh

The Clouds DSP in `dsp/`, `resources.*`, `shy_fft.h`, `stmtemp.h`, `buffer_allocator.h` and `parameter_interpolator.h` is unchanged from the Daisy port. Only `nembo.cpp` is specific to the Carciofo: it maps the pots, CV, buttons and LED onto Clouds and switches its modes at runtime.
