#include <math.h>

#include "../../lib/carciofo.h"
#include "daisysp.h"
#include "granular_processor.h"

using namespace daisy;
using namespace carciofo;

/*
 * Nembo turns the Carciofo into Clouds, the texture synthesizer by Emilie
 * Gillet (Mutable Instruments), through Ben Sergentanis' Daisy port (Nimbus).
 * The Loewenzahnhonig had one build per Clouds mode; the Carciofo has two
 * buttons and an RGB LED, so all four modes, the fidelity switch and freeze
 * now live in one firmware.
 *
 * B1 steps the playback mode; hold it to toggle the lo-fi buffer. Tap B2 to
 * freeze, hold B2 to reach a second pot layer with the blend controls. CV1 is
 * added to the playback position, CV2 is the freeze/trigger gate. The output
 * is fully wet by default and always soft-clipped, as on Clouds.
 */

namespace {

enum Role { kPosition, kSize, kPitch, kDensity, kTexture, kFeedback };

// Primary pot roles per playback mode (granular, stretch, looping delay,
// spectral). The order matches PlaybackMode.
constexpr Role kRole[PLAYBACK_MODE_LAST][4] = {
    {kPosition, kSize, kPitch, kDensity},    // granular
    {kPosition, kSize, kPitch, kTexture},    // stretch
    {kPosition, kFeedback, kPitch, kSize},   // looping delay
    {kPosition, kSize, kPitch, kTexture},    // spectral
};

// Whether the fourth pot of the shift layer is texture (else feedback). It is
// chosen per mode so it never fights a primary pot for the same parameter.
constexpr bool kShiftP4Texture[PLAYBACK_MODE_LAST] = {true, false, true, false};

// LED hue per mode.
constexpr float kModeHue[PLAYBACK_MODE_LAST] = {0.33f, 0.50f, 0.62f, 0.86f};

constexpr float kPitchDeadZone = 0.04f;
constexpr float kPitchRangeSemitones = 24.f;
constexpr float kFreezeOn = 0.55f;
constexpr float kFreezeOff = 0.45f;

constexpr uint32_t kLongPressMs = 400;
constexpr uint32_t kTapMs = 260;
constexpr uint32_t kFlashMs = 160;

// Boot values of the shift layer: fully wet, a little reverb, centred spread,
// and a moderate texture/feedback.
constexpr float kShiftBoot[4] = {1.0f, 0.2f, 0.5f, 0.4f};

float PitchFromPot(float pot) {
  float offset = pot - 0.5f;
  float magnitude = fabsf(offset) - kPitchDeadZone;
  if (magnitude <= 0.f) return 0.f;
  float x = magnitude / (0.5f - kPitchDeadZone);
  float semitones = kPitchRangeSemitones * x * x;
  return offset < 0.f ? -semitones : semitones;
}

}  // namespace

static Carciofo hw;
static GranularProcessorClouds processor;

// Sample memory and FX workspace, sized like the original Clouds firmware.
uint8_t block_mem[118784];
uint8_t block_ccm[65536 - 128];

static Parameters* parameters;

static int mode = PLAYBACK_MODE_GRANULAR;
static bool lofi = false;
static bool freezeLatch = false;
static bool cvFreeze = false;
static bool shift = false, prevShift = false;

static float primary[4], secondary[4];
static bool caught[4];
static float lastPot[4];
static float basePosition = 0.f;

static bool b1LongHandled = false;
static uint32_t b2PressStart = 0;
static uint32_t flashTime = 0;
static bool reinit = false;

static float PotAt(int i) {
  return hw.GetPot(static_cast<Pot>(i));
}

// Two pot layers with soft pickup: a pot only takes hold of its stored value
// once it has been turned back through it, so switching layers never jumps.
static void TrackPots() {
  float* layer = shift ? secondary : primary;
  if (shift != prevShift) {
    for (int i = 0; i < 4; i++) caught[i] = fabsf(PotAt(i) - layer[i]) < 0.02f;
    prevShift = shift;
  }
  for (int i = 0; i < 4; i++) {
    float p = PotAt(i);
    if (!caught[i]) {
      bool crossed = (p - layer[i]) * (lastPot[i] - layer[i]) < 0.f;
      if (fabsf(p - layer[i]) < 0.02f || crossed) caught[i] = true;
    }
    if (caught[i]) layer[i] = p;
    lastPot[i] = p;
  }
}

static void ApplyControls() {
  parameters->dry_wet = secondary[0];
  parameters->reverb = secondary[1];
  parameters->stereo_spread = secondary[2];
  parameters->feedback = 0.15f;
  parameters->texture = 0.7f;
  if (kShiftP4Texture[mode]) {
    parameters->texture = secondary[3];
  } else {
    parameters->feedback = secondary[3];
  }

  for (int i = 0; i < 4; i++) {
    float v = primary[i];
    switch (kRole[mode][i]) {
      case kPosition: basePosition = v; break;
      case kSize: parameters->size = v; break;
      case kPitch: parameters->pitch = PitchFromPot(v); break;
      case kDensity: parameters->density = v; break;
      case kTexture: parameters->texture = v; break;
      case kFeedback: parameters->feedback = v; break;
    }
  }
}

static void HandleButtons() {
  uint32_t now = System::GetNow();

  if (hw.button[BUTTON_1].RisingEdge()) b1LongHandled = false;
  if (hw.button[BUTTON_1].Pressed() && !b1LongHandled &&
      hw.button[BUTTON_1].TimeHeldMs() >= kLongPressMs) {
    lofi = !lofi;
    reinit = true;
    b1LongHandled = true;
    flashTime = now;
  }
  if (hw.button[BUTTON_1].FallingEdge() && !b1LongHandled) {
    mode = (mode + 1) % PLAYBACK_MODE_LAST;
    processor.set_playback_mode(static_cast<PlaybackMode>(mode));
    flashTime = now;
  }

  if (hw.button[BUTTON_2].RisingEdge()) b2PressStart = now;
  shift = hw.button[BUTTON_2].Pressed();
  if (hw.button[BUTTON_2].FallingEdge() && now - b2PressStart < kTapMs) {
    freezeLatch = !freezeLatch;
    flashTime = now;
  }
}

static void UpdateLed() {
  uint32_t now = System::GetNow();
  bool frozen = freezeLatch || cvFreeze;
  float hue = kModeHue[mode];
  float sat = lofi ? 0.55f : 0.9f;

  if (now - flashTime < kFlashMs) {
    hw.led.SetHsv(hue, sat, 1.f);
    return;
  }
  if (shift) {
    hw.led.SetHsv(hue, 0.35f, 0.55f);
    return;
  }
  if (frozen) {
    float pulse = 0.5f - 0.5f * cosf(TWOPI_F * (now % 1600) / 1600.f);
    hw.led.SetHsv(hue, sat, 0.45f + 0.45f * pulse);
    return;
  }
  float breath = 0.5f - 0.5f * cosf(TWOPI_F * (now % 4000) / 4000.f);
  hw.led.SetHsv(hue, sat, 0.05f + 0.12f * breath);
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  const float gate = hw.GetCv(CV_2);
  const bool was = cvFreeze;
  if (gate > kFreezeOn) {
    cvFreeze = true;
  } else if (gate < kFreezeOff) {
    cvFreeze = false;
  }
  parameters->freeze = freezeLatch || cvFreeze;
  parameters->trigger = cvFreeze && !was;
  parameters->position = Clamp(basePosition + hw.GetCv(CV_1), 0.f, 1.f);

  FloatFrame input[32];
  FloatFrame output[32];
  for (size_t i = 0; i < size; i++) {
    input[i].l = in[0][i];
    input[i].r = in[1][i];
    output[i].l = output[i].r = 0.f;
  }

  processor.Process(input, output, size);

  for (size_t i = 0; i < size; i++) {
    out[0][i] = daisysp::SoftClip(output[i].l);
    out[1][i] = daisysp::SoftClip(output[i].r);
  }
}

int main(void) {
  hw.Init(32);  // Clouds does not work with larger blocks.
  float sampleRate = hw.SampleRate();

  InitResources(sampleRate);
  processor.Init(sampleRate, block_mem, sizeof(block_mem), block_ccm,
                 sizeof(block_ccm));
  processor.set_playback_mode(PLAYBACK_MODE_GRANULAR);
  processor.set_quality(0);

  parameters = processor.mutable_parameters();
  parameters->dry_wet = 1.f;
  parameters->stereo_spread = 0.5f;
  parameters->freeze = false;
  parameters->trigger = false;
  parameters->gate = false;

  for (int i = 0; i < 4; i++) {
    primary[i] = PotAt(i);
    secondary[i] = kShiftBoot[i];
    caught[i] = true;
    lastPot[i] = primary[i];
  }
  ApplyControls();

  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();
    HandleButtons();
    TrackPots();
    ApplyControls();
    if (reinit) {
      reinit = false;
      processor.set_quality(lofi ? 3 : 0);
    }
    UpdateLed();
    processor.Prepare();
  }
}
