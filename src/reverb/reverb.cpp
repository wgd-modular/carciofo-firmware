#include <cmath>

#include "../../lib/carciofo.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace carciofo;

/*
 * Stereo reverb with a freeze and a shimmer.
 *
 * B1 freezes the tail: the send into the tank drops to nothing and the
 * feedback goes to unity, so whatever is in there at that moment stays there.
 * B2 folds an octave up copy of the tank output back into its own input,
 * which is where the shimmer comes from. The octave path keeps running while
 * the tank is frozen, so holding both gives a drone that builds by itself.
 * Both latch, press again to leave.
 *
 * P1 dry level    P2 wet level
 * P3 decay        P4 tone
 * CV1 decay       CV2 tone
 *
 * The LED tracks the decay, cyan into violet while the reverb is plain and
 * yellow into red once the shimmer is in. Brightness follows the wet level.
 * A frozen tail overrides all of that with a slow breath, white on its own
 * and amber with the shimmer running.
 */

static const float kMinDecay = 0.70f;
static const float kMaxDecay = 0.98f;
// The octave path adds gain of its own, so the tank has to give some back.
static const float kMaxDecayShimmer = 0.90f;
static const float kShimmerSend = 0.50f;
/* How hot the tail is allowed to get before the octave send is pulled back.
   A frozen tank plus the octave path settles rather than runs away, but it
   settles somewhere that depends on the material and it can land above full
   scale, so the level is measured and capped. Set high enough that it only
   catches the peaks and leaves the build up alone. */
static const float kShimmerCeiling = 0.80f;

static const float kFreezeFeedback = 1.00f;
// A lossless tank cannot take the octave path on top of it, the level would
// just keep climbing, so the shimmer freezes a hair short of unity.
static const float kFreezeFeedbackShimmer = 0.99f;
// Damping is a loss around the loop and would eat a held tail within a second,
// so the tone control steps aside while the tank is frozen.
static const float kFreezeTone = 18000.f;

// One pole coefficients, roughly 40 ms for the send ramps and 200 ms for the
// tail level, both at 48 kHz.
static const float kSmoothing = 0.0005f;
static const float kEnvSmoothing = 0.0001f;

static Carciofo hw;

static ReverbSc DSY_SDRAM_BSS tank;
static PitchShifter DSY_SDRAM_BSS octaveLeft;
static PitchShifter DSY_SDRAM_BSS octaveRight;
static DcBlock dcLeft, dcRight;

static bool frozen, shimmering;
static float dryLevel, wetLevel;
static float sendTarget = 1.f, shimmerTarget;
static float send = 1.f, shimmer;

// Previous output of the tank, this is what the octave path picks up.
static float tailLeft, tailRight, tailEnv;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  for (size_t i = 0; i < size; i++) {
    send += kSmoothing * (sendTarget - send);
    shimmer += kSmoothing * (shimmerTarget - shimmer);

    float dryL = in[0][i];
    float dryR = in[1][i];

    /* Soft clipped so the octave loop settles instead of running away, and
       stripped of its DC before it goes back in. The shifter and the clipper
       both leave an offset behind and the tank has no way to shed one, so
       without the blocker it walks into the rail and the module goes quiet. */
    float octaveL = dcLeft.Process(SoftClip(octaveLeft.Process(tailLeft)));
    float octaveR = dcRight.Process(SoftClip(octaveRight.Process(tailRight)));

    tailEnv +=
        kEnvSmoothing * (fmaxf(fabsf(tailLeft), fabsf(tailRight)) - tailEnv);
    float octaveGain = shimmer;
    if (tailEnv > kShimmerCeiling) octaveGain *= kShimmerCeiling / tailEnv;

    // Only the dry send is gated, the octave path feeds a frozen tank too.
    float sendL = dryL * send + octaveL * octaveGain;
    float sendR = dryR * send + octaveR * octaveGain;
    tank.Process(sendL, sendR, &tailLeft, &tailRight);

    out[0][i] = dryL * dryLevel + tailLeft * wetLevel;
    out[1][i] = dryR * dryLevel + tailRight * wetLevel;
  }
}

static void UpdateLed(float decay) {
  if (frozen) {
    float breath =
        0.5f + 0.5f * sinf(TWOPI_F * (System::GetNow() % 2000) / 2000.f);
    hw.led.SetHsv(0.08f, shimmering ? 0.8f : 0.f, 0.15f + 0.55f * breath);
    return;
  }

  float hue = shimmering ? 0.13f - 0.13f * decay : 0.45f + 0.30f * decay;
  hw.led.SetHsv(hue, 0.9f, 0.1f + 0.9f * wetLevel);
}

int main(void) {
  hw.Init();

  float sampleRate = hw.SampleRate();

  tank.Init(sampleRate);
  tank.SetFeedback(kMinDecay);
  tank.SetLpFreq(12000.f);

  octaveLeft.Init(sampleRate);
  octaveLeft.SetTransposition(12.f);
  octaveRight.Init(sampleRate);
  octaveRight.SetTransposition(12.f);

  dcLeft.Init(sampleRate);
  dcRight.Init(sampleRate);

  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();

    if (hw.button[BUTTON_1].RisingEdge()) frozen = !frozen;
    if (hw.button[BUTTON_2].RisingEdge()) shimmering = !shimmering;

    dryLevel = hw.GetPot(POT_1);
    wetLevel = hw.GetPot(POT_2);

    float decay = hw.GetPotWithCv(POT_3, CV_1);
    float tone = hw.GetPotWithCv(POT_4, CV_2);

    /* Feedback and damping go straight in rather than through a ramp. They set
       the decay rate instead of sitting on the signal, so a step does not
       click, and ramping them would let the tail fade away underneath the
       freeze before it ever reached unity. */
    if (frozen) {
      tank.SetFeedback(shimmering ? kFreezeFeedbackShimmer : kFreezeFeedback);
      tank.SetLpFreq(kFreezeTone);
      sendTarget = 0.f;
    } else {
      float maxDecay = shimmering ? kMaxDecayShimmer : kMaxDecay;
      tank.SetFeedback(kMinDecay + decay * (maxDecay - kMinDecay));
      tank.SetLpFreq(400.f * exp2f(tone * 5.5f));  // 400 Hz to 18 kHz
      sendTarget = 1.f;
    }
    shimmerTarget = shimmering ? kShimmerSend : 0.f;

    UpdateLed(decay);
    System::Delay(1);
  }
}
