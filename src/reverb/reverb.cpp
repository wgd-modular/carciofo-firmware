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
 * which is where the shimmer comes from. Both latch, press again to leave.
 *
 * P1 dry level    P2 wet level
 * P3 decay        P4 tone
 * CV1 decay       CV2 tone
 *
 * The LED tracks the decay, cyan into violet while the reverb is plain and
 * yellow into red once the shimmer is in. Brightness follows the wet level.
 * A frozen tail overrides all of that with a slow white breath.
 */

static const float kMinDecay = 0.70f;
static const float kMaxDecay = 0.98f;
// The octave path adds gain of its own, so the tank has to give some back.
static const float kMaxDecayShimmer = 0.90f;
static const float kFreezeFeedback = 1.00f;
static const float kShimmerSend = 0.50f;

// One pole coefficient for the parameter ramps, roughly 40 ms at 48 kHz.
static const float kSmoothing = 0.0005f;

static Carciofo hw;

static ReverbSc DSY_SDRAM_BSS tank;
static PitchShifter DSY_SDRAM_BSS octaveLeft;
static PitchShifter DSY_SDRAM_BSS octaveRight;

static bool frozen, shimmering;
static float dryLevel, wetLevel;
static float sendTarget = 1.f, feedbackTarget = kMinDecay, shimmerTarget;
static float send = 1.f, feedback = kMinDecay, shimmer;

// Previous output of the tank, this is what the octave path picks up.
static float tailLeft, tailRight;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  for (size_t i = 0; i < size; i++) {
    send += kSmoothing * (sendTarget - send);
    shimmer += kSmoothing * (shimmerTarget - shimmer);
    feedback += kSmoothing * (feedbackTarget - feedback);
    tank.SetFeedback(feedback);

    float dryL = in[0][i];
    float dryR = in[1][i];

    // Soft clipped so the octave loop settles instead of running away.
    float shimmerL = SoftClip(octaveLeft.Process(tailLeft)) * shimmer;
    float shimmerR = SoftClip(octaveRight.Process(tailRight)) * shimmer;

    float sendL = (dryL + shimmerL) * send;
    float sendR = (dryR + shimmerR) * send;
    tank.Process(sendL, sendR, &tailLeft, &tailRight);

    out[0][i] = dryL * dryLevel + tailLeft * wetLevel;
    out[1][i] = dryR * dryLevel + tailRight * wetLevel;
  }
}

static void UpdateLed(float decay) {
  if (frozen) {
    float breath =
        0.5f + 0.5f * sinf(TWOPI_F * (System::GetNow() % 2000) / 2000.f);
    hw.led.SetHsv(0.f, 0.f, 0.15f + 0.55f * breath);
    return;
  }

  float hue = shimmering ? 0.13f - 0.13f * decay : 0.45f + 0.30f * decay;
  hw.led.SetHsv(hue, 0.9f, 0.1f + 0.9f * wetLevel);
}

int main(void) {
  hw.Init();

  float sampleRate = hw.SampleRate();

  tank.Init(sampleRate);
  tank.SetFeedback(feedback);
  tank.SetLpFreq(12000.f);

  octaveLeft.Init(sampleRate);
  octaveLeft.SetTransposition(12.f);
  octaveRight.Init(sampleRate);
  octaveRight.SetTransposition(12.f);

  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();

    if (hw.button[BUTTON_1].RisingEdge()) frozen = !frozen;
    if (hw.button[BUTTON_2].RisingEdge()) shimmering = !shimmering;

    dryLevel = hw.GetPot(POT_1);
    wetLevel = hw.GetPot(POT_2);

    float decay = hw.GetPotWithCv(POT_3, CV_1);
    float tone = hw.GetPotWithCv(POT_4, CV_2);

    if (frozen) {
      feedbackTarget = kFreezeFeedback;
      sendTarget = 0.f;
    } else {
      float maxDecay = shimmering ? kMaxDecayShimmer : kMaxDecay;
      feedbackTarget = kMinDecay + decay * (maxDecay - kMinDecay);
      sendTarget = 1.f;
    }
    shimmerTarget = shimmering ? kShimmerSend : 0.f;

    tank.SetLpFreq(400.f * exp2f(tone * 5.5f));  // 400 Hz to 18 kHz

    UpdateLed(decay);
    System::Delay(1);
  }
}
