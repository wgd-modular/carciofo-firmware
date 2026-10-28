# Coda

Coda is a set of clocked performance effects for the end of the chain, for builds, drops, breakdowns and fills in a live set. Every knob is clean at noon. Turn it left or right for two flavours of its effect, and B1 drops all of them in or out on the next beat. Coda is Italian for tail, and the name of the passage that ends a piece of music.

All four effects run at once and stack. Each one starts gently just off noon and gets stronger toward the end stop. Leave a knob at noon and that effect is out of the way.

```
repeat / tape stop -> pump / gate -> echo / reverb -> filter
```

## Controls

| Control | Function |
|---------|----------|
| P1 | Filter: low-pass to the left, high-pass to the right, clean at noon |
| P2 | Space: reverb to the left, echo on 3/4 of a beat to the right |
| P3 | Time: tape stop to the left, beat repeat to the right |
| P4 | Pulse: sidechain pump to the left, gate to the right |
| CV1 | Clock in, 1, 4 or 24 pulses per beat |
| CV2 | Throws the linked effects toward their end stops, 0 to +5 V |
| B1 | Tap to switch all effects in or out on the next beat, hold for a momentary switch, double tap to cut everything at once |
| B2 | Tap for tap tempo (with a clock: marks beat 1); hold and nudge a pot to link it to CV2 |
| In L/R | Stereo audio in |
| Out L/R | Stereo audio out |

## The four knobs

- **Filter.** A DJ filter over everything, echo and reverb tails included. Its resonance rises as it closes. To the right it is a high-pass, and over its last stretch a noise riser fades in under it for builds.
- **Space.** To the left a reverb wash, to the right an echo on 3/4 of a beat, the classic DJ echo. Both are sends, so their tails ring on when you turn back or switch the effects out. Low end stays out of both, so the kick stays dry and tight. The last stretch toward either end stop takes the dry signal away. Fully right is an echo-out, the trails of what you just played ringing into silence. Fully left is a wash of reverb with no dry signal.
- **Time.** To the right a beat repeat. It fades in at a whole beat, then steps through 1/8, 1/8 triplet, 1/16, 1/16 triplet and 1/32 as you turn. The slice is grabbed on the grid and kept, so turning further makes the roll faster without losing its place. Back at noon you are straight back in time with the music. To the left a tape stop: on the next beat the music winds down to a halt, slowly near noon (about a bar) and fast toward the end stop (half a beat). It stays stopped until you bring P3 back to noon, and then comes back on the beat.
- **Pulse.** To the right a gate that chops the sound in time. It fades in at 1/8, then steps through 1/16, 1/16 triplet and 1/32. To the left a sidechain pump that ducks the sound on every beat and swells back, deeper the further you turn.

## Clock

Patch a clock into CV1. Coda works out by itself whether it gets quarter notes, sixteenths (4 pulses per beat) or 24 pulses per beat, from how far apart the pulses are. That works for quarter notes up to about 230 BPM, sixteenths from about 60 BPM up, and 24 pulses per beat at any usual tempo. A clock that starts again after a pause marks beat 1. When the clock stops, repeat, tape stop, gate and pump step aside and let the sound through, and the echo keeps its last tempo.

Without a clock Coda runs on its own tempo, 120 BPM out of the box. Tap B2 in time to set it; the first tap of a run is beat 1. Without a clock nothing waits for a beat: B1 switches at once, and switching in starts the bar there, so repeats and gates line up with your press.

## Switching on the beat

With a clock, a tap on B1 is held back until the next beat and lands exactly on it, so you can press a little early and the drop is still tight. A press up to 40 ms after a beat still counts for that beat. While a switch is waiting, the LED blinks fast.

- **Tap B1** to switch all effects in or out. With the effects out you can set up the knobs silently, then drop everything in on the beat.
- **Hold B1** for a momentary switch: it lands on the beat, and letting go switches back on the next beat. Good for fills.
- **Double tap B1** to cut everything straight away, echo and reverb tails included. The panic button.

Echo and reverb tails keep ringing when the effects are switched out with a single tap.

## CV2

CV2 throws the effects linked to it. Each linked effect moves from where its pot is toward the end stop on the side the pot is turned to. An effect sitting at noon is pushed to the right. With nothing patched CV2 does nothing.

- A slow ramp into CV2 is a hands-free build: link the filter, the time and the pulse knobs, set them just off noon, and the high-pass, the roll and the gate all speed up together.
- A gate from a sequencer with the time knob linked and at noon plays rolls exactly where you program them.
- An LFO with the filter linked is a wobble.

Out of the box only the filter is linked. Hold B2 and give a pot a small nudge to link or unlink it. Each pot can be changed once per hold, and the effects ignore the pots while B2 is held. Holding B2 shows the linked effects one after the other in their colours.

## Saved settings

The CV2 links and the tapped tempo come back after a power cycle. The knobs are read from the pots, and the effects always start switched in. Hold both buttons while powering up to go back to the defaults.

## LED

Each pot has a colour: filter blue, space green, time red, pulse amber. A left-hand flavour shows a little paler.

- **Beat**: the LED ticks once per beat, and each beat of the bar belongs to one pot. Beat 1 shows the filter, beat 2 space, beat 3 time and beat 4 pulse, in that effect's colour if it is on and as a faint white tick if it is at noon. One bar reads out the whole set. With the effects switched out the same ticks show dim.
- **Waiting switch**: fast blinking until it lands on the beat.
- **Turning a pot**: its colour, brighter the further it is turned. A short white flash when it lands back at noon, so you can find noon in the dark.
- **Tape stopped**: steady red until P3 returns to noon.
- **Clock stopped**: a slow, dim white breathing.
- **Holding B2**: the linked effects one after the other. A nudge confirms with that pot's colour when linked, and goes dark when unlinked.
- **Tapping B2**: a white flash per tap.

## Credits

Written for the Carciofo. The reverb is DaisySP's ReverbSc. The idea of transition effects at the end of the chain comes from DJ mixers, the OXI META and the Ohm Force BOHM Performer. Coda does not port their code.
