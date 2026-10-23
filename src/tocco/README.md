# Tocco

A channel strip for two mono inputs, always live and always in the same order:

```
in 1 -> level/saturation -> eq -> dj filter -> ducker -+-> limiter -> L + R
in 2 -> level -+--------------(sidechain)--------^     |
               +-> saturation -> eq -> mix ------------+
```

In 1 is the signal you shape. In 2 drives the ducker and can be mixed in on
top. Both end up summed on the two outputs, left and right carry the same
signal.

## Layers

Each effect is a layer, and the four pots only reach the layer you are on. B2
steps to the next layer, B1 back to the previous one, both wrap around.

| Layer | P1 | P2 | P3 | P4 |
|-------|----|----|----|----|
| Level | in 1 level | in 1 drive | in 2 level | in 2 drive |
| EQ | high gain | mid gain | mid freq | low gain |
| Filter | lowpass | lowpass resonance | highpass | highpass resonance |
| Ducker | threshold | amount | release | in 2 mix |

**Level** has unity at noon and +12 dB fully up. Drive pushes the input into a
tanh saturation and divides it back down, so it changes colour rather than
loudness. At zero it is a clean thru.

**EQ** runs from high to low across the pots: a high shelf at 8 kHz, a mid
bell with a fixed Q of 1 swept from 150 Hz to 8 kHz, and a low shelf at 100 Hz.
Every band goes ±15 dB, flat at noon. The same settings shape in 2 on its way
to the output, while the ducker still listens to in 2 before the EQ. The shelves have no resonant bump, they
simply tilt up or down past their corner.

**Filter** is a DJ style lowpass into a highpass, each with its own resonance.
Both pots work the same way: fully left the filter is open and out of the
signal entirely, turning right closes it. The lowpass closes down to 60 Hz, the
highpass closes up to 6 kHz. Pulling both in makes a band pass.
CV1 closes the lowpass and CV2 closes the highpass, added on top of the pot.
Resonance stops just short of self oscillation.

**Ducker** follows in 2 with a fixed 5 ms attack and pulls in 1 down by the
amount, recovering over the release, 30 ms to 1 s. Amount fully up ducks in 1
to silence. Threshold sets how loud in 2 has to be for that full duck, from half
scale fully left down to -34 dB fully right, so turn it up for a quiet
sidechain. In 2 level on the level layer scales the sidechain as well. The ducker listens to in 2 before its drive, so in 2 drive only
colours what is mixed into the output. In 2 mix sets how loud in 2 itself is in the output, at zero
it is only the sidechain.

A fixed peak limiter sits on the summed output so two hot inputs never clip.
Anything below about 0.7 of full scale passes at unity, louder peaks are caught
quickly and released over about a second.

## Soft pickup

Switching a layer leaves the pots parked away from the values stored
underneath. A pot stays quiet until you turn it back through its stored value,
then it takes hold. Nothing jumps. The LED blinks while a pot is still being
hunted for and blips white the moment it lands.

## Saved settings

Every pot value on every layer, and the layer you were on, is saved to the
Daisy's flash two seconds after the last change and comes back at power up.
Since the pots no longer match what was saved, every layer waits to be picked
up, so the patch you hear after power up is exactly the one you left.

To start over, hold both buttons while powering up. The LED lights white until
you let go, and everything goes back to neutral: unity level, no drive, flat
EQ, both filters open (fully left), no ducking, threshold low, in 2 mixed in at
unity.

## LED

- **Idle** it breathes in the colour of the current layer: level red, EQ amber,
  filter blue, ducker violet.
- **Turning a pot** shows the layer colour with the brightness on the value,
  blinking until the pot is caught.

## Controls

| Control | Function |
|---------|----------|
| P1 - P4 | Parameters of the current layer |
| CV1 | Closes the lowpass, on top of P1 in the filter layer |
| CV2 | Closes the highpass, on top of P3 in the filter layer |
| B1 | Previous layer |
| B2 | Next layer |
| L in | In 1, the signal |
| R in | In 2, the sidechain |
| L out | Mix |
| R out | Mix, same as left |
