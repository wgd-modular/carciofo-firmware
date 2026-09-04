#include <cmath>

#include "../../lib/carciofo.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace carciofo;

enum Model { KICK = 0, SNARE, HAT, TOM, CLAP, kNumModels };
enum Flavor { FLAVOR_808 = 0, FLAVOR_909, kNumFlavors };

using Hat808 = HiHat<SquareNoise, SwingVCA>;
using Hat909 = HiHat<RingModNoise, LinearVCA>;

static const float kTrigMargin = 0.25f;
static const float kTrigRelease = 0.10f;
static const int kTrigWarmup = 96;
static const float kAuditionVel = 0.85f;

static const int kClapBursts = 4;
static const float kClapIntervalMs = 9.f;

static const uint32_t kLongPressMs = 320;
static const uint32_t kFlashMs = 150;
static const uint32_t kSelectMs = 900;

static const float kModelGain[kNumModels] = {0.90f, 0.85f, 0.70f, 0.90f, 0.80f};

static const float kModelRgb[kNumModels][3] = {
    {1.00f, 0.05f, 0.00f},  // kick, red
    {1.00f, 0.45f, 0.00f},  // snare, amber
    {0.00f, 0.80f, 1.00f},  // hat, cyan
    {1.00f, 0.20f, 0.00f},  // tom, orange
    {1.00f, 0.00f, 0.65f},  // clap, magenta
};

static inline float Expo(float t, float lo, float hi) {
  return lo * powf(hi / lo, t);
}

struct Channel {
  AnalogBassDrum bd808;
  SyntheticBassDrum bd909;
  AnalogSnareDrum sd808;
  SyntheticSnareDrum sd909;
  Hat808 hh808;
  Hat909 hh909;

  int model = KICK;
  int flavor = FLAVOR_808;
  float tune = 0.5f;
  float decay = 0.5f;

  bool triggerArmed = false;
  float trigBaseline = 0.5f;
  int warmup = kTrigWarmup;
  float auditionVel = 0.f;
  bool longHandled = false;

  bool clapRunning = false;
  int clapStep = 0;
  int clapTimer = 0;
  float clapVel = 0.f;
  int clapInterval = 432;

  uint32_t flashTime = 0;
  float flashVel = 0.f;
  int flashModel = KICK;
  uint32_t selectTime = 0;

  float sampleRate = 48000.f;

  void Init(float sr) {
    sampleRate = sr;
    clapInterval = static_cast<int>(kClapIntervalMs * 0.001f * sr);
    bd808.Init(sr);
    bd909.Init(sr);
    sd808.Init(sr);
    sd909.Init(sr);
    hh808.Init(sr);
    hh909.Init(sr);
  }

  void ApplyParams() {
    switch (model) {
      case KICK:
        if (flavor == FLAVOR_808) {
          bd808.SetFreq(Expo(tune, 30.f, 120.f));
          bd808.SetDecay(0.10f + 0.90f * decay);
          bd808.SetTone(0.50f);
          bd808.SetSelfFmAmount(0.35f);
          bd808.SetAttackFmAmount(0.40f);
        } else {
          bd909.SetFreq(Expo(tune, 30.f, 120.f));
          bd909.SetDecay(0.10f + 0.90f * decay);
          bd909.SetTone(0.60f);
          bd909.SetDirtiness(0.25f);
          bd909.SetFmEnvelopeAmount(0.70f);
          bd909.SetFmEnvelopeDecay(0.40f);
        }
        break;
      case TOM:
        if (flavor == FLAVOR_808) {
          bd808.SetFreq(Expo(tune, 80.f, 400.f));
          bd808.SetDecay(0.30f + 0.70f * decay);
          bd808.SetTone(0.70f);
          bd808.SetSelfFmAmount(0.10f);
          bd808.SetAttackFmAmount(0.20f);
        } else {
          bd909.SetFreq(Expo(tune, 80.f, 400.f));
          bd909.SetDecay(0.30f + 0.70f * decay);
          bd909.SetTone(0.70f);
          bd909.SetDirtiness(0.10f);
          bd909.SetFmEnvelopeAmount(0.30f);
          bd909.SetFmEnvelopeDecay(0.30f);
        }
        break;
      case SNARE:
        if (flavor == FLAVOR_808) {
          sd808.SetFreq(Expo(tune, 120.f, 320.f));
          sd808.SetDecay(0.10f + 0.90f * decay);
          sd808.SetSnappy(0.65f);
          sd808.SetTone(0.50f);
        } else {
          sd909.SetFreq(Expo(tune, 120.f, 320.f));
          sd909.SetDecay(0.10f + 0.90f * decay);
          sd909.SetSnappy(0.70f);
          sd909.SetFmAmount(0.30f);
        }
        break;
      case HAT:
        if (flavor == FLAVOR_808) {
          hh808.SetFreq(Expo(tune, 1500.f, 8000.f));
          hh808.SetDecay(0.90f * decay);
          hh808.SetTone(0.55f);
          hh808.SetNoisiness(0.80f);
        } else {
          hh909.SetFreq(Expo(tune, 1500.f, 8000.f));
          hh909.SetDecay(0.90f * decay);
          hh909.SetTone(0.70f);
          hh909.SetNoisiness(0.55f);
        }
        break;
      case CLAP:
        break;
    }
  }

  void TrigVoice(float vel) {
    switch (model) {
      case KICK:
      case TOM:
        if (flavor == FLAVOR_808) {
          bd808.SetAccent(vel);
          bd808.Trig();
        } else {
          bd909.SetAccent(vel);
          bd909.Trig();
        }
        break;
      case SNARE:
        if (flavor == FLAVOR_808) {
          sd808.SetAccent(vel);
          sd808.Trig();
        } else {
          sd909.SetAccent(vel);
          sd909.Trig();
        }
        break;
      case HAT:
        if (flavor == FLAVOR_808) {
          hh808.SetAccent(vel);
          hh808.Trig();
        } else {
          hh909.SetAccent(vel);
          hh909.Trig();
        }
        break;
      case CLAP:
        clapVel = vel;
        clapStep = 0;
        clapTimer = 0;
        clapRunning = true;
        break;
    }
  }

  void Fire(float vel) {
    flashTime = System::GetNow();
    flashVel = vel;
    flashModel = model;
    TrigVoice(vel);
  }

  void DetectTrigger(float cv) {
    if (auditionVel > 0.f) {
      Fire(auditionVel);
      auditionVel = 0.f;
    }
    if (warmup > 0) {
      trigBaseline += 0.05f * (cv - trigBaseline);
      warmup--;
      return;
    }
    if (!triggerArmed) {
      trigBaseline += 0.01f * (cv - trigBaseline);
      if (cv > trigBaseline + kTrigMargin) {
        triggerArmed = true;
        Fire(Clamp(0.40f + 1.20f * (cv - trigBaseline), 0.40f, 1.f));
      }
    } else if (cv < trigBaseline + kTrigRelease) {
      triggerArmed = false;
    }
  }

  float ProcessClap() {
    if (clapRunning && clapTimer <= 0) {
      bool last = clapStep == kClapBursts - 1;
      float burstDecay = last ? (0.15f + 0.50f * decay) : 0.06f;
      if (flavor == FLAVOR_808) {
        sd808.SetFreq(Expo(tune, 300.f, 1200.f));
        sd808.SetSnappy(1.0f);
        sd808.SetTone(0.70f);
        sd808.SetDecay(burstDecay);
        sd808.SetAccent(clapVel);
        sd808.Trig();
      } else {
        sd909.SetFreq(Expo(tune, 300.f, 1200.f));
        sd909.SetSnappy(1.0f);
        sd909.SetFmAmount(0.f);
        sd909.SetDecay(burstDecay);
        sd909.SetAccent(clapVel);
        sd909.Trig();
      }
      clapStep++;
      if (last)
        clapRunning = false;
      else
        clapTimer = clapInterval;
    }
    if (clapTimer > 0) clapTimer--;
    return flavor == FLAVOR_808 ? sd808.Process() : sd909.Process();
  }

  float Process() {
    float s;
    switch (model) {
      case KICK:
      case TOM:
        s = flavor == FLAVOR_808 ? bd808.Process() : bd909.Process();
        break;
      case SNARE:
        s = flavor == FLAVOR_808 ? sd808.Process() : sd909.Process();
        break;
      case HAT:
        s = flavor == FLAVOR_808 ? hh808.Process() : hh909.Process();
        break;
      case CLAP:
        s = ProcessClap();
        break;
      default:
        s = 0.f;
        break;
    }
    return SoftClip(s * kModelGain[model]);
  }
};

static Carciofo hw;
static Channel chA, chB;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  chA.DetectTrigger(hw.GetCv(CV_1));
  chB.DetectTrigger(hw.GetCv(CV_2));
  for (size_t i = 0; i < size; i++) {
    out[0][i] = chA.Process();
    out[1][i] = chB.Process();
  }
}

static void UpdateChannel(Channel& ch, Button button, Pot tunePot,
                          Pot decayPot) {
  ch.tune = hw.GetPot(tunePot);
  ch.decay = hw.GetPot(decayPot);

  if (hw.button[button].RisingEdge()) ch.longHandled = false;

  if (hw.button[button].Pressed() && !ch.longHandled &&
      hw.button[button].TimeHeldMs() >= kLongPressMs) {
    ch.flavor ^= 1;
    ch.longHandled = true;
    ch.selectTime = System::GetNow();
    ch.auditionVel = kAuditionVel;
  }

  if (hw.button[button].FallingEdge() && !ch.longHandled) {
    ch.model = (ch.model + 1) % kNumModels;
    ch.selectTime = System::GetNow();
    ch.auditionVel = kAuditionVel;
  }

  ch.ApplyParams();
}

static void AddFlash(const Channel& ch, uint32_t now, float& r, float& g,
                     float& b) {
  uint32_t age = now - ch.flashTime;
  if (age >= kFlashMs) return;
  float level = ch.flashVel * (1.f - static_cast<float>(age) / kFlashMs);
  const float* rgb = kModelRgb[ch.flashModel];
  r += rgb[0] * level;
  g += rgb[1] * level;
  b += rgb[2] * level;
}

static void SelectionColour(const Channel& ch, float& r, float& g, float& b) {
  const float* rgb = kModelRgb[ch.model];
  float tint = ch.flavor == FLAVOR_909 ? 0.45f : 0.f;
  float v = 0.6f;
  r = (rgb[0] + (1.f - rgb[0]) * tint) * v;
  g = (rgb[1] + (1.f - rgb[1]) * tint) * v;
  b = (rgb[2] + (1.f - rgb[2]) * tint) * v;
}

static void UpdateLed() {
  uint32_t now = System::GetNow();

  float r = 0.f, g = 0.f, b = 0.f;
  AddFlash(chA, now, r, g, b);
  AddFlash(chB, now, r, g, b);
  if (r + g + b > 0.01f) {
    hw.led.Set(Clamp(r, 0.f, 1.f), Clamp(g, 0.f, 1.f), Clamp(b, 0.f, 1.f));
    return;
  }

  bool selA = now - chA.selectTime < kSelectMs;
  bool selB = now - chB.selectTime < kSelectMs;
  if (selA || selB) {
    const Channel& ch =
        (selA && selB) ? (chA.selectTime > chB.selectTime ? chA : chB)
                       : (selA ? chA : chB);
    SelectionColour(ch, r, g, b);
    hw.led.Set(r, g, b);
    return;
  }

  float blend = 0.5f - 0.5f * cosf(TWOPI_F * (now % 3000) / 3000.f);
  const float* a = kModelRgb[chA.model];
  const float* c = kModelRgb[chB.model];
  float v = 0.10f;
  hw.led.Set((a[0] + (c[0] - a[0]) * blend) * v,
             (a[1] + (c[1] - a[1]) * blend) * v,
             (a[2] + (c[2] - a[2]) * blend) * v);
}

int main(void) {
  __set_FPSCR(__get_FPSCR() | (1UL << 24));

  hw.Init();
  float sampleRate = hw.SampleRate();

  chA.Init(sampleRate);
  chB.Init(sampleRate);
  chA.ApplyParams();
  chB.ApplyParams();

  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();
    UpdateChannel(chA, BUTTON_1, POT_1, POT_3);
    UpdateChannel(chB, BUTTON_2, POT_2, POT_4);
    UpdateLed();
    System::Delay(1);
  }
}
