# Hardware Test

## Description

Bring up firmware for checking a freshly built module. It touches every control on the panel and needs nothing but a pair of headphones.

On power up the LED runs through red, green, blue and white so all three channels get proven. After that it tracks one analog control at a time, every control has its own colour and the brightness follows the value, so turning the selected pot fades the LED up.

- B1: step to the next control, in the order P1, P2, P3, P4, CV1, CV2
- B2: swap the audio path between the input jacks and a pair of test tones, 440 Hz on the left and 660 Hz on the right. The seed's onboard LED lights while they run

The same readings go out over USB serial four times a second if you want the actual numbers:

```
P1 +0.501 P2 +0.000 P3 +1.000 P4 +0.250 CV1 +0.500 CV2 +0.500 B1 0 B2 0
```
