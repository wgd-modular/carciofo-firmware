# Reverb

Turns the Carciofo into a stereo reverb with a freeze and an octave up shimmer.

## Description

Freeze mutes the send into the tank and takes the feedback to unity, so whatever is in there at that moment holds; the damping is opened up at the same time, otherwise it would eat the held tail within a second. Shimmer folds an octave up copy of the tank output back into its own input, and since the octave path keeps running while the tank is frozen, holding both gives you a drone that climbs on its own. The decay range gets pulled in a little while the shimmer is on, the octave path adds gain of its own and the tank has to give some back for the loop to settle instead of running away.

## Controls

| Control | Function      |
|---------|---------------|
| P1      | Dry Level     |
| P2      | Wet Level     |
| P3      | Decay         |
| P4      | Tone          |
| CV1     | Decay CV      |
| CV2     | Tone CV       |
| B1      | Freeze        |
| B2      | Shimmer       |

Both buttons latch, press again to leave. The tone pot has no effect while the tank is frozen.

The LED sits on the decay, cyan into violet while the reverb is plain and yellow into red once the shimmer is in. Brightness follows the wet level. A frozen tail overrides all of that with a slow breath, white on its own and amber with the shimmer running.
