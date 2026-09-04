#include <cmath>
#include <cstdint>

#include "../../lib/carciofo.h"
#include "daisysp.h"

extern "C" float powf(float base, float power) {
  if (base <= 0.f) return 0.f;
  union {
    float f;
    uint32_t i;
  } vx = {base}, mx;
  mx.i = (vx.i & 0x007fffffu) | 0x3f000000u;
  float lg = vx.i * 1.1920929e-7f - 124.22552f - 1.4980303f * mx.f -
             1.72588f / (0.35208872f + mx.f);
  float p = power * lg;
  if (p < -126.f) p = -126.f;
  if (p > 126.f) p = 126.f;
  float offset = p < 0.f ? 1.f : 0.f;
  int w = static_cast<int>(p);
  float z = p - w + offset;
  union {
    uint32_t i;
    float f;
  } v;
  v.i = static_cast<uint32_t>(
      8388608.f * (p + 121.274055f + 27.728023f / (4.8425255f - z) -
                   1.4901291f * z));
  return v.f;
}

extern "C" float sinf(float x) {
  x -= 6.2831853f * floorf(x * 0.15915494f + 0.5f);
  if (x > 1.5707963f) x = 3.1415927f - x;
  if (x < -1.5707963f) x = -3.1415927f - x;
  float x2 = x * x;
  return x *
         (0.9999966f + x2 * (-0.16664824f + x2 * (0.00830629f -
                                                  x2 * 0.00018363f)));
}

using namespace daisy;
using namespace daisysp;
using namespace carciofo;

enum Model { KICK = 0, SNARE, HAT, CLAP, kNumModels };
enum Flavor { FLAVOR_808 = 0, FLAVOR_909, kNumFlavors };

using Hat808 = HiHat<SquareNoise, SwingVCA>;
using Hat909 = HiHat<RingModNoise, LinearVCA>;

static const float kTrigRise = 0.20f;
static const float kTrigRearm = 0.07f;
static const float kBaselineSlew = 0.02f;
static const int kRefractoryMs = 20;
static const float kVelFloor = 0.35f;
static const float kAuditionVel = 0.85f;

static const float kClapSpikeMs = 10.5f;
static const float kClapSpikeTau = 0.004f;
static const float kClapTailLevel = 0.55f;

static const uint32_t kLongPressMs = 320;
static const uint32_t kFlashMs = 150;
static const uint32_t kSelectMs = 900;

static const float kModelGain[kNumModels] = {0.90f, 0.85f, 0.70f, 0.85f};

static const float kModelRgb[kNumModels][3] = {
    {1.00f, 0.05f, 0.00f},
    {1.00f, 0.45f, 0.00f},
    {0.00f, 0.80f, 1.00f},
    {1.00f, 0.00f, 0.65f},
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
  WhiteNoise clapNoise;
  Svf clapFilter;

  volatile int model;
  volatile int flavor;
  volatile float tune;
  volatile float decay;
  volatile int pendingTrig;
  volatile float pendingVel;

  float baseline;
  bool armed;
  int refractoryMs;
  bool baselineSettled;
  int settleMs;
  bool longHandled;

  float snareEnv;
  float snareEnvMult;
  float clapEnv;
  float clapTailMult;
  int clapSpikesLeft;
  int clapSpikeTimer;
  int clapSpikeSamples;
  bool clapInTail;
  float clapVel;
  float sampleRate;

  uint32_t flashTime;
  float flashVel;
  int flashModel;
  uint32_t selectTime;

  void Init(float sr) {
    model = KICK;
    flavor = FLAVOR_808;
    tune = 0.5f;
    decay = 0.5f;
    pendingTrig = 0;
    pendingVel = 0.f;
    baseline = 0.5f;
    armed = false;
    refractoryMs = 0;
    baselineSettled = false;
    settleMs = 100;
    longHandled = false;
    snareEnv = 0.f;
    snareEnvMult = 0.999f;
    clapEnv = 0.f;
    clapTailMult = 0.99f;
    clapSpikesLeft = 0;
    clapSpikeTimer = 0;
    clapSpikeSamples = static_cast<int>(kClapSpikeMs * 0.001f * sr);
    clapInTail = false;
    clapVel = 0.f;
    sampleRate = sr;
    flashTime = 0;
    flashVel = 0.f;
    flashModel = KICK;
    selectTime = 0;

    bd808.Init(sr);
    bd909.Init(sr);
    sd808.Init(sr);
    sd909.Init(sr);
    hh808.Init(sr);
    hh909.Init(sr);
    clapNoise.Init();
    clapFilter.Init(sr);
  }

  void PollTrigger(float cv) {
    if (refractoryMs > 0) refractoryMs--;
    if (settleMs > 0) {
      settleMs--;
      baseline += 0.2f * (cv - baseline);
      return;
    }
    if (!armed) {
      baseline += kBaselineSlew * (cv - baseline);
      if (refractoryMs == 0 && cv > baseline + kTrigRise) {
        armed = true;
        refractoryMs = kRefractoryMs;
        Fire(Clamp(kVelFloor + 1.3f * (cv - baseline), kVelFloor, 1.f));
      }
    } else if (cv < baseline + kTrigRearm) {
      armed = false;
    }
  }

  void Fire(float vel) {
    flashTime = System::GetNow();
    flashVel = vel;
    flashModel = model;
    pendingVel = vel;
    pendingTrig = 1;
  }

  void ApplyParams(int m, int f, float t, float d) {
    switch (m) {
      case KICK:
        if (f == FLAVOR_808) {
          float f0 = Expo(t, 30.f, 120.f);
          float res = 0.45f + 0.52f * d;
          float q = res * sampleRate / (0.4f * f0);
          bd808.SetFreq(f0);
          bd808.SetDecay(1.5f * log2f(q / 1500.f) + 1.f);
          bd808.SetTone(0.45f);
          bd808.SetSelfFmAmount(0.60f);
          bd808.SetAttackFmAmount(0.60f);
        } else {
          bd909.SetFreq(Expo(t, 30.f, 120.f));
          bd909.SetDecay(0.05f + 1.00f * d);
          bd909.SetTone(0.60f);
          bd909.SetDirtiness(0.25f);
          bd909.SetFmEnvelopeAmount(0.70f);
          bd909.SetFmEnvelopeDecay(0.40f);
        }
        break;
      case SNARE:
        if (f == FLAVOR_808) {
          sd808.SetFreq(Expo(t, 120.f, 320.f));
          sd808.SetDecay(0.10f + 0.90f * d);
          sd808.SetSnappy(0.65f);
          sd808.SetTone(0.50f);
          snareEnvMult = expf(-1.f / (Expo(d, 0.04f, 0.45f) * sampleRate));
        } else {
          sd909.SetFreq(Expo(t, 120.f, 320.f));
          sd909.SetDecay(0.10f + 0.90f * d);
          sd909.SetSnappy(0.70f);
          sd909.SetFmAmount(0.30f);
        }
        break;
      case HAT:
        if (f == FLAVOR_808) {
          hh808.SetFreq(Expo(t, 1500.f, 8000.f));
          hh808.SetDecay(0.90f * d);
          hh808.SetTone(0.55f);
          hh808.SetNoisiness(0.80f);
        } else {
          hh909.SetFreq(Expo(t, 1500.f, 8000.f));
          hh909.SetDecay(0.90f * d);
          hh909.SetTone(0.70f);
          hh909.SetNoisiness(0.55f);
        }
        break;
      case CLAP: {
        float centre = Expo(t, 800.f, 1900.f) * (f == FLAVOR_909 ? 1.3f : 1.f);
        clapFilter.SetFreq(centre);
        clapFilter.SetRes(f == FLAVOR_909 ? 0.45f : 0.60f);
        float tail = Expo(d, 0.030f, 0.50f);
        clapTailMult = expf(-1.f / (tail * sampleRate));
        break;
      }
    }
  }

  void RenderBlock(float* out, size_t size) {
    int m = model;
    int f = flavor;
    float t = tune;
    float d = decay;
    if (m < 0 || m >= kNumModels) m = KICK;

    ApplyParams(m, f, t, d);

    if (pendingTrig) {
      pendingTrig = 0;
      float vel = pendingVel;
      switch (m) {
        case KICK:
          if (f == FLAVOR_808) {
            bd808.SetAccent(vel);
            bd808.Trig();
          } else {
            bd909.SetAccent(vel);
            bd909.Trig();
          }
          break;
        case SNARE:
          if (f == FLAVOR_808) {
            sd808.SetAccent(vel);
            sd808.Trig();
            snareEnv = 1.f;
          } else {
            sd909.SetAccent(vel);
            sd909.Trig();
          }
          break;
        case HAT:
          if (f == FLAVOR_808) {
            hh808.SetAccent(vel);
            hh808.Trig();
          } else {
            hh909.SetAccent(vel);
            hh909.Trig();
          }
          break;
        case CLAP:
          clapVel = vel;
          clapEnv = 1.f;
          clapSpikesLeft = f == FLAVOR_909 ? 3 : 2;
          clapSpikeTimer = clapSpikeSamples;
          clapInTail = false;
          break;
      }
    }

    float finiteCheck = 0.f;
    for (size_t i = 0; i < size; i++) {
      float s;
      switch (m) {
        case KICK:
          s = f == FLAVOR_808 ? bd808.Process() * 1.6f : bd909.Process();
          break;
        case SNARE:
          if (f == FLAVOR_808) {
            s = sd808.Process() * snareEnv;
            snareEnv *= snareEnvMult;
          } else {
            s = sd909.Process();
          }
          break;
        case HAT:
          s = f == FLAVOR_808 ? hh808.Process() : hh909.Process();
          break;
        case CLAP:
          s = ProcessClap();
          break;
        default:
          s = 0.f;
          break;
      }
      finiteCheck += s;
      out[i] = SoftClip(s * kModelGain[m]);
    }
    if (!(finiteCheck - finiteCheck == 0.f)) {
      float sr = sampleRate;
      bd808.Init(sr);
      bd909.Init(sr);
      sd808.Init(sr);
      sd909.Init(sr);
      hh808.Init(sr);
      hh909.Init(sr);
      clapFilter.Init(sr);
      clapEnv = 0.f;
      for (size_t i = 0; i < size; i++) out[i] = 0.f;
    }
  }

  float ProcessClap() {
    if (clapSpikesLeft > 0) {
      clapEnv *= 1.f - 1.f / (kClapSpikeTau * sampleRate);
      if (--clapSpikeTimer <= 0) {
        clapSpikesLeft--;
        if (clapSpikesLeft > 0) {
          clapEnv = 1.f;
          clapSpikeTimer = clapSpikeSamples;
        } else {
          clapEnv = kClapTailLevel;
          clapInTail = true;
        }
      }
    } else if (clapInTail) {
      clapEnv *= clapTailMult;
      if (clapEnv < 1e-4f) clapInTail = false;
    } else {
      return 0.f;
    }
    clapFilter.Process(clapNoise.Process());
    return SoftClip(clapFilter.Band() * clapEnv * clapVel * 4.0f);
  }
};

static Carciofo hw;
static Channel chA, chB;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  chA.RenderBlock(out[0], size);
  chB.RenderBlock(out[1], size);
}

static void UpdateChannel(Channel& ch, Button button, Pot tunePot,
                          Pot decayPot, Cv cv) {
  ch.tune = hw.GetPot(tunePot);
  ch.decay = hw.GetPot(decayPot);
  ch.PollTrigger(hw.GetCv(cv));

  if (hw.button[button].RisingEdge()) ch.longHandled = false;

  if (hw.button[button].Pressed() && !ch.longHandled &&
      hw.button[button].TimeHeldMs() >= kLongPressMs) {
    ch.flavor = ch.flavor == FLAVOR_808 ? FLAVOR_909 : FLAVOR_808;
    ch.longHandled = true;
    ch.selectTime = System::GetNow();
    ch.Fire(kAuditionVel);
  }

  if (hw.button[button].FallingEdge() && !ch.longHandled) {
    ch.model = (ch.model + 1) % kNumModels;
    ch.selectTime = System::GetNow();
    ch.Fire(kAuditionVel);
  }
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
  float breath = 0.5f - 0.5f * cosf(TWOPI_F * (now % 4000) / 4000.f);
  const float* a = kModelRgb[chA.model];
  const float* c = kModelRgb[chB.model];
  float v = 0.04f + 0.10f * breath;
  hw.led.Set((a[0] + (c[0] - a[0]) * blend) * v,
             (a[1] + (c[1] - a[1]) * blend) * v,
             (a[2] + (c[2] - a[2]) * blend) * v);
}

static void RunBootSweep() {
  static const float steps[][3] = {
      {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}};
  for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
    hw.led.Set(steps[i][0], steps[i][1], steps[i][2]);
    hw.led.Update();
    System::Delay(150);
  }
}

int main(void) {
  hw.Init();
  float sampleRate = hw.SampleRate();

  chA.Init(sampleRate);
  chB.Init(sampleRate);

  RunBootSweep();
  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();
    UpdateChannel(chA, BUTTON_1, POT_1, POT_3, CV_1);
    UpdateChannel(chB, BUTTON_2, POT_2, POT_4, CV_2);
    UpdateLed();
    System::Delay(1);
  }
}
