# Doppio

Turns the Carciofo into two independent drum voices, one per output, each fired by a trigger.

## Description

The module splits down the middle. The left half, P1, P3, CV1, B1 and the left output, is one drum voice; the right half, P2, P4, CV2, B2 and the right output, is another just like it. Nothing crosses between them.

Each voice is triggered from its CV input, so you patch a gate or trigger sequencer in and it plays. The trigger is picked up per audio block and its height sets the velocity, a full gate hits hard and a smaller one plays softer, so the voices stay dynamic.

Every voice can be any of five drums, and each drum comes in an 808 and a 909 flavour, built on the analog and synthetic engine pairs from DaisySP:

| Model | 808 | 909 |
|-------|-----|-----|
| Kick  | analog bass drum | synthetic bass drum |
| Snare | analog snare | synthetic snare |
| Hat   | square-noise, swing VCA | ring-mod noise, linear VCA |
| Tom   | analog bass drum, tuned up | synthetic bass drum, tuned up |
| Clap  | analog snare, multi-tap | synthetic snare, multi-tap |

The clap is a burst of four fast snare hits, the last one left to ring, which is where the hand-clap character comes from.

A short press on the button steps to the next model and wraps around. A long press toggles the 808 or 909 flavour. Either one fires the voice once so you hear the change straight away, which also lets you audition a drum with nothing patched into the trigger.

## Controls

| Control | Function |
|---------|----------|
| P1 | Left tune |
| P3 | Left decay |
| CV1 | Left trigger in |
| B1 | Left model, hold to switch 808 / 909 |
| P2 | Right tune |
| P4 | Right decay |
| CV2 | Right trigger in |
| B2 | Right model, hold to switch 808 / 909 |
| L out | Left voice |
| R out | Right voice |

Tune sets the pitch of the drum, the metallic pitch on the hat and the noise colour on the clap. Decay sets its length, which on the hat is the difference between closed and open.

## LED

One light shared by both voices.

- **On power up** it sweeps red, green, blue, white, so you know the firmware is running before anything is patched.
- **On every trigger** it flashes the colour of the drum that fired, kick red, snare amber, hat cyan, tom orange, clap magenta, with the brightness on the velocity. Two hits at once blend.
- **While you work a button** it holds that voice's current model as a steady colour, saturated for 808 and washed lighter for 909. Which voice it is you know from the button under your finger.
- **At rest** it breathes slowly while drifting between the two voices' colours, so a glance tells you what both sides are set to and that the module is alive.
