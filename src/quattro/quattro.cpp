#include <cmath>

#include "../../lib/carciofo.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace carciofo;

/*
 * Quattro. Four effects in one, always live and always in the same order:
 *
 *   drive -> chorus -> delay -> reverb
 *
 * The trick to fitting twelve parameters onto four pots is that the two
 * buttons are shift keys, not switches. Each pot owns one effect and the pot
 * it owns never changes, only which of that effect's parameters it reaches:
 *
 *   no button   character   drive amount   chorus rate   delay time   size
 *   hold B1     motion      drive tone     chorus depth  feedback     rev tone
 *   hold B2     mix         the wet level of each of the four effects
 *
 * P1 drive   P2 chorus   P3 delay   P4 reverb. B2 wins if both are held.
 *
 * Each effect crossfades its own wet against the signal that reaches it, so a
 * mix at zero is a true bypass and all four mixes at zero is a clean thru.
 *
 * CV1 rides the delay time and CV2 rides the reverb size, on top of the pot
 * and regardless of the layer, so both stay live while your hands are on the
 * character controls.
 *
 * Shifting a layer strands the pot away from the value stored underneath it,
 * so a pot only takes hold once it has been turned back through that stored
 * value. Nothing jumps. The LED calls out which pot is being caught.
 */

enum Fx { FX_DRIVE = 0, FX_CHORUS, FX_DELAY, FX_REVERB, kNumFx };
enum Layer { LAYER_CHARACTER = 0, LAYER_MOTION, LAYER_MIX, kNumLayers };

// A little over a second of delay per channel, parked in the SDRAM.
static const size_t kDelayMax = 48000;

// One pole coefficient, roughly 40 ms at 48 kHz. Rides the crossfades, the
// delay time and the feedback so that a step on any of them glides in.
static const float kSmoothing = 0.0005f;

// How close the pot has to be to the stored value to be treated as landed on
// it, and how far it has to move before the LED calls it the active pot.
static const float kCatchEps = 0.02f;
static const float kMoveEps = 0.004f;

// A move keeps owning the LED for this long after the pot stops turning.
static const uint32_t kEditHoldMs = 1100;

// Fixed lowpass sitting in the delay feedback path so the repeats darken as
// they stack rather than turning to noise.
static const float kDelayDamp = 3500.f;

// Identity colour per effect, used both for the editing readout and blended by
// mix for the idle glow.
static const float kFxRgb[kNumFx][3] = {
    {1.00f, 0.15f, 0.00f},  // drive, red
    {0.10f, 1.00f, 0.20f},  // chorus, green
    {0.00f, 0.55f, 1.00f},  // delay, blue
    {0.55f, 0.10f, 1.00f},  // reverb, violet
};

static Carciofo hw;

static Overdrive driveLeft, driveRight;
static Tone driveToneLeft, driveToneRight;
static Chorus chorus;
static DelayLine<float, kDelayMax> DSY_SDRAM_BSS delayLeft, delayRight;
static Tone dampLeft, dampRight;
static ReverbSc DSY_SDRAM_BSS reverb;

// Written by the control loop, read by the audio callback. Aligned 32 bit
// loads and stores are atomic on the M7, so the same lockless handoff the
// other firmwares use holds here.
static float mixTarget[kNumFx];
static float delayTimeTarget = 12000.f;
static float delayFbTarget;

// Smoothed copies the callback actually runs on.
static float mix[kNumFx];
static float delayTime = 12000.f;
static float delayFb;

// The value behind every pot on every layer, 0 to 1, plus whether the pot has
// been caught since the layer was last entered.
static float knob[kNumFx][kNumLayers];
static bool caught[kNumFx][kNumLayers];
static float lastRaw[kNumFx];

static Layer layer = LAYER_CHARACTER;
static int activeFx;
static uint32_t lastMoveTime;
static uint32_t catchFlashTime;

static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

// t of 0 to 1 across a frequency ratio, spaced by ear rather than by hertz.
static inline float Expo(float t, float lo, float hi) {
  return lo * powf(hi / lo, t);
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  for (size_t i = 0; i < size; i++) {
    for (int f = 0; f < kNumFx; f++)
      mix[f] += kSmoothing * (mixTarget[f] - mix[f]);
    delayTime += kSmoothing * (delayTimeTarget - delayTime);
    delayFb += kSmoothing * (delayFbTarget - delayFb);

    float left = in[0][i];
    float right = in[1][i];

    // Drive. Two mono stages so the stereo image survives into the chorus.
    float driveL = driveToneLeft.Process(driveLeft.Process(left));
    float driveR = driveToneRight.Process(driveRight.Process(right));
    left = Lerp(left, driveL, mix[FX_DRIVE]);
    right = Lerp(right, driveR, mix[FX_DRIVE]);

    // Chorus. Fed the mono sum, folded back in as a widened wet layer so the
    // dry image is untouched until the mix is brought up.
    chorus.Process(0.5f * (left + right));
    left = Lerp(left, chorus.GetLeft(), mix[FX_CHORUS]);
    right = Lerp(right, chorus.GetRight(), mix[FX_CHORUS]);

    // Delay. Read on a fractional tap so a moving time glides instead of
    // stepping, feedback darkened a touch each pass.
    float echoL = delayLeft.ReadHermite(delayTime);
    float echoR = delayRight.ReadHermite(delayTime);
    delayLeft.Write(left + dampLeft.Process(echoL) * delayFb);
    delayRight.Write(right + dampRight.Process(echoR) * delayFb);
    left = Lerp(left, echoL, mix[FX_DELAY]);
    right = Lerp(right, echoR, mix[FX_DELAY]);

    // Reverb. ReverbSc hands back a wet only pair, so the crossfade is against
    // the signal going in.
    float wetL, wetR;
    reverb.Process(left, right, &wetL, &wetR);
    left = Lerp(left, wetL, mix[FX_REVERB]);
    right = Lerp(right, wetR, mix[FX_REVERB]);

    out[0][i] = left;
    out[1][i] = right;
  }
}

/* Runs the soft pickup for one pot on the active layer and reports how far it
   moved this pass. A pot that has not been caught leaves its stored value
   alone until the pot is turned back through it. */
static float TrackPot(int fx, float raw) {
  float stored = knob[fx][layer];
  if (!caught[fx][layer]) {
    bool crossed = (raw - stored) * (lastRaw[fx] - stored) < 0.f;
    if (fabsf(raw - stored) < kCatchEps || crossed) {
      caught[fx][layer] = true;
      catchFlashTime = System::GetNow();
    }
  }
  if (caught[fx][layer]) knob[fx][layer] = raw;
  float moved = fabsf(raw - lastRaw[fx]);
  lastRaw[fx] = raw;
  return moved;
}

// Maps the twelve stored values onto the engines. The mixes, the delay time
// and its feedback go through the smoothed targets, everything else sets a
// coefficient and can go straight in.
static void ApplyControls() {
  float driveAmt = knob[FX_DRIVE][LAYER_CHARACTER];
  driveLeft.SetDrive(driveAmt);
  driveRight.SetDrive(driveAmt);

  float driveTone = Expo(knob[FX_DRIVE][LAYER_MOTION], 800.f, 18000.f);
  driveToneLeft.SetFreq(driveTone);
  driveToneRight.SetFreq(driveTone);

  chorus.SetLfoFreq(Expo(knob[FX_CHORUS][LAYER_CHARACTER], 0.05f, 6.f));
  chorus.SetLfoDepth(0.95f * knob[FX_CHORUS][LAYER_MOTION]);

  float delayTimeMs =
      Expo(Clamp(knob[FX_DELAY][LAYER_CHARACTER] + hw.GetCv(CV_1), 0.f, 1.f),
           20.f, 1000.f);
  delayTimeTarget = delayTimeMs * 0.001f * hw.SampleRate();
  delayFbTarget = 0.95f * knob[FX_DELAY][LAYER_MOTION];

  float size =
      Clamp(knob[FX_REVERB][LAYER_CHARACTER] + hw.GetCv(CV_2), 0.f, 1.f);
  reverb.SetFeedback(Lerp(0.70f, 0.94f, size));
  reverb.SetLpFreq(Expo(knob[FX_REVERB][LAYER_MOTION], 500.f, 18000.f));

  for (int f = 0; f < kNumFx; f++) mixTarget[f] = knob[f][LAYER_MIX];
}

static void UpdateLed() {
  uint32_t now = System::GetNow();

  // A caught pot throws a short white blip, the tactile click of it landing.
  if (now - catchFlashTime < 110) {
    hw.led.Set(1.f, 1.f, 1.f);
    return;
  }

  // A pot turned in the last moment owns the readout: its effect's colour,
  // brightness on the value, styled by the layer it is reaching into.
  if (now - lastMoveTime < kEditHoldMs) {
    const float* rgb = kFxRgb[activeFx];
    float value = 0.12f + 0.88f * knob[activeFx][layer];

    if (layer == LAYER_MOTION) {  // the moving layer breathes
      float breath = 0.5f + 0.5f * sinf(TWOPI_F * (now % 1200) / 1200.f);
      value *= 0.45f + 0.55f * breath;
    }
    // The mix layer washes toward white to read as level rather than colour.
    float wash = layer == LAYER_MIX ? 0.55f : 1.f;
    float white = layer == LAYER_MIX ? (1.f - wash) * value : 0.f;

    if (!caught[activeFx][layer]) {  // still hunting for the stored value
      bool on = (now % 260) < 130;
      value *= on ? 1.f : 0.12f;
      white *= on ? 1.f : 0.12f;
    }

    hw.led.Set(rgb[0] * wash * value + white, rgb[1] * wash * value + white,
               rgb[2] * wash * value + white);
    return;
  }

  // A held button with no pot moving yet just says which layer is armed:
  // motion breathes, mix sits steady, both close to white so no effect colour
  // is implied.
  bool b1 = hw.button[BUTTON_1].Pressed();
  bool b2 = hw.button[BUTTON_2].Pressed();
  if (b1 || b2) {
    if (b2) {
      hw.led.Set(0.16f, 0.16f, 0.16f);
    } else {
      float breath = 0.5f + 0.5f * sinf(TWOPI_F * (now % 1600) / 1600.f);
      float v = 0.05f + 0.16f * breath;
      hw.led.Set(v, v, v);
    }
    return;
  }

  // Idle. The patch shown as one colour, every effect's identity weighted by
  // how wet it is, breathing on the total.
  float r = 0.f, g = 0.f, b = 0.f, total = 0.f;
  for (int f = 0; f < kNumFx; f++) {
    r += kFxRgb[f][0] * knob[f][LAYER_MIX];
    g += kFxRgb[f][1] * knob[f][LAYER_MIX];
    b += kFxRgb[f][2] * knob[f][LAYER_MIX];
    total += knob[f][LAYER_MIX];
  }
  float breath = 0.5f + 0.5f * sinf(TWOPI_F * (now % 5000) / 5000.f);
  if (total < 0.05f) {  // a clean thru still shows a faint sign of life
    float v = 0.03f + 0.03f * breath;
    hw.led.Set(v, v, v);
    return;
  }
  float level = (0.10f + 0.30f * breath) / total;
  hw.led.Set(r * level, g * level, b * level);
}

int main(void) {
  hw.Init();
  float sampleRate = hw.SampleRate();

  driveLeft.Init();
  driveRight.Init();
  driveToneLeft.Init(sampleRate);
  driveToneRight.Init(sampleRate);

  chorus.Init(sampleRate);
  chorus.SetFeedback(0.2f);

  delayLeft.Init();
  delayRight.Init();
  dampLeft.Init(sampleRate);
  dampRight.Init(sampleRate);
  dampLeft.SetFreq(kDelayDamp);
  dampRight.SetFreq(kDelayDamp);

  reverb.Init(sampleRate);

  /* Boot straight onto the pots for the character layer, so what you see is
     what you hear, and onto a gentle stock patch for the two shifted layers.
     The character layer is caught from the start, the others wait to be picked
     up. */
  static const float kMotionBoot[kNumFx] = {0.70f, 0.35f, 0.35f, 0.60f};
  static const float kMixBoot[kNumFx] = {0.00f, 0.20f, 0.22f, 0.35f};
  for (int f = 0; f < kNumFx; f++) {
    float raw = hw.GetPot(static_cast<Pot>(f));
    knob[f][LAYER_CHARACTER] = raw;
    knob[f][LAYER_MOTION] = kMotionBoot[f];
    knob[f][LAYER_MIX] = kMixBoot[f];
    caught[f][LAYER_CHARACTER] = true;
    lastRaw[f] = raw;
    mix[f] = mixTarget[f] = kMixBoot[f];
  }
  ApplyControls();

  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();

    Layer next = hw.button[BUTTON_2].Pressed()
                     ? LAYER_MIX
                     : (hw.button[BUTTON_1].Pressed() ? LAYER_MOTION
                                                      : LAYER_CHARACTER);
    if (next != layer) {
      layer = next;
      // Re entering a layer, any pot already sitting on its stored value is
      // caught for free, the rest have to be turned back to it.
      for (int f = 0; f < kNumFx; f++) {
        caught[f][layer] =
            fabsf(hw.GetPot(static_cast<Pot>(f)) - knob[f][layer]) < kCatchEps;
      }
    }

    float bestMove = kMoveEps;
    int moved = -1;
    for (int f = 0; f < kNumFx; f++) {
      float m = TrackPot(f, hw.GetPot(static_cast<Pot>(f)));
      if (m > bestMove) {
        bestMove = m;
        moved = f;
      }
    }
    if (moved >= 0) {
      activeFx = moved;
      lastMoveTime = System::GetNow();
    }

    ApplyControls();
    UpdateLed();
    System::Delay(1);
  }
}
