# Eco

Eco is a clockable stereo delay for the Carciofo, ported from Nik Ansell's Smoodge Delay on the Löwenzahnhonig. It runs up to sixteen seconds of delay with effectively no minimum time, syncs to an incoming clock, and folds in "smoodge" — a tape-flavoured spatial effect that widens the stereo field and adds wow, flutter, saturation and a clock-linked tremolo as it is turned up. Where the original needed a clock into a jack, the Carciofo's buttons let you tap the tempo in by hand, freeze the buffer into an infinite loop, and read the tempo back off the LED.

## Controls

The delay time is a division or multiplication of the tempo. Set the tempo with a clock into CV1 or by tapping B1; P3 then scales it, from an eighth of the clock at fully counter-clockwise to eight times it at fully clockwise, with unison in the centre. Without a clock or taps the delay starts at half a second.

| Control | Function |
|---------|----------|
| P1 | Feedback — one repeat at minimum, self-oscillating loop at maximum |
| P2 | Dry / wet — fully dry at minimum, fully wet at maximum |
| P3 | Delay time as a ratio of the tempo (see below) |
| P4 | Smoodge — stereo width, then wow, flutter, saturation and tremolo |
| CV1 | Clock in — sets the tempo from the time between pulses |
| CV2 | Adds to feedback, or to smoodge (see B2); centred, so an unpatched jack does nothing |
| B1 | Tap tempo — tap in rhythm to set the delay time |
| B2 | Tap to freeze the buffer; hold to switch CV2 between feedback and smoodge |
| In L/R | Stereo audio in (normal In L to In R in hardware for a mono source) |
| Out L/R | Stereo audio out, soft limited |

P3 ratios, counter-clockwise to clockwise: /8, /4, /3, /2, /1.5, x1, x2, x3, x4, x5, x8. The /3 and /1.5 steps give half-note and whole-note triplets against the clock.

Freeze locks the current buffer into an endless loop with the input muted, so you can lift your source and keep the echo going, or hold a texture under a new part. Tap B2 again to release it. Hold B2 to flip what CV2 does: by default CV2 pushes the feedback up alongside P1; in the other mode it drives the smoodge instead, so an envelope or LFO can open up the tape effects.

## LED

- **Idle**: a dim breathing glow. Its colour tracks the delay ratio — blue for the long, multiplied settings, green around unison, red for the short, divided ones.
- **Tempo**: the LED ticks once per echo, at the actual delay time, so you can see the tempo and dial the ratio in by eye. Brighter ticks mean more feedback.
- **Frozen**: a slow magenta pulse.
- **CV2 mode change** (hold B2): a short confirmation flash — green when CV2 controls feedback, amber when it controls smoodge.

## Credits

Ported from the Smoodge Delay by Nik Ansell (gamecat69) on the wgd modular Löwenzahnhonig, MIT licensed. The delay, chorus and tremolo are DaisySP. The Carciofo build keeps the original's sound and adds tap tempo, freeze and the LED tempo readout using the two buttons and RGB LED the Löwenzahnhonig did not have.
