#include <cmath>
#include <cstdint>

#include "../../lib/carciofo.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace carciofo;

enum Mode { SKIN = 0, LIQUID, METAL, kNumModes };

static const int kPartials = 6;

static const float kTrigRise = 0.20f;
static const float kTrigRearm = 0.07f;
static const float kBaselineSlew = 0.02f;
static const int kRefractoryMs = 12;
static const float kVelFloor = 0.35f;
static const float kAuditionVel = 0.85f;

static const float kFreqSmooth = 0.002f;
static const float kSmooth = 0.004f;
static const float kPitchEnvAmt = 1.6f;

static const uint32_t kFlashMs = 130;
static const uint32_t kSelectMs = 700;

static const float kModeHue[kNumModes] = {0.09f, 0.55f, 0.85f};

static const int kOS = 2;
static const float kModeGain[kNumModes] = {1.0f, 1.05f, 0.90f};
static const float kHarmRatio[kPartials] = {1.f, 2.f, 3.f, 4.f, 5.f, 6.f};
static const float kPrimeRatio[kPartials] = {1.f, 3.f, 5.f, 7.f, 11.f, 13.f};

static const float kPrimaryBoot[4] = {0.42f, 0.30f, 0.20f, 0.42f};
static const float kSecondaryBoot[4] = {0.20f, 0.55f, 0.50f, 0.00f};

static inline float Expo(float t, float lo, float hi) {
  return lo * powf(hi / lo, t);
}

static inline float FastSin(float phase) {
  float a = 6.2831853f * (phase - floorf(phase));
  if (a > 3.1415927f) a -= 6.2831853f;
  if (a > 1.5707963f) a = 3.1415927f - a;
  if (a < -1.5707963f) a = -3.1415927f - a;
  float a2 = a * a;
  return a * (0.9999966f + a2 * (-0.16664824f + a2 * (0.00830629f -
                                                      a2 * 0.00018363f)));
}

static inline float Blep(float t, float dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.f;
  }
  if (t > 1.f - dt) {
    t = (t - 1.f) / dt;
    return t * t + t + t + 1.f;
  }
  return 0.f;
}

static inline float MorphWave(float phase, float dt, float morph) {
  float sine = FastSin(phase);
  float tri = 1.f - 4.f * fabsf(phase - 0.5f);
  float saw = (2.f * phase - 1.f) - Blep(phase, dt);
  float t2 = phase < 0.5f ? phase + 0.5f : phase - 0.5f;
  float sqr = (phase < 0.5f ? 1.f : -1.f) + Blep(phase, dt) -
              Blep(t2, dt);
  if (morph < 0.3333f) {
    float t = morph * 3.f;
    return sine + (tri - sine) * t;
  }
  if (morph < 0.6667f) {
    float t = (morph - 0.3333f) * 3.f;
    return tri + (saw - tri) * t;
  }
  float t = (morph - 0.6667f) * 3.f;
  return saw + (sqr - saw) * t;
}

static Carciofo hw;

static uint32_t rngState = 0x1234567u;
static inline float Noise() {
  rngState = rngState * 1664525u + 1013904223u;
  return static_cast<int32_t>(rngState) * (1.f / 2147483648.f);
}

static float sampleRate = 48000.f;

static volatile int mode = SKIN;
static volatile float freqTarget = 110.f;
static volatile float morphTarget = 0.3f;
static volatile float foldTarget = 0.2f;
static volatile float decayCoefTarget = 0.9995f;
static volatile float attackInc = 0.02f;
static volatile float noiseTarget = 0.f;
static volatile float ratioTarget[kPartials] = {1, 2, 3, 4, 5, 6};
static volatile float ampTarget[kPartials] = {1, 0, 0, 0, 0, 0};
static volatile float relCoefTarget[kPartials] = {1, 1, 1, 1, 1, 1};
static volatile float modRatioTarget = 2.f;
static volatile float indexTarget = 1.f;
static volatile int pendingTrig = 0;
static volatile float pendingVel = 0.f;

static float freq = 110.f, morph = 0.3f, fold = 0.2f, noiseAmt = 0.f;
static float ratio[kPartials], amp[kPartials], modRatio = 2.f, fmIndex = 1.f;
static float relCoef[kPartials] = {1, 1, 1, 1, 1, 1};
static float rdec[kPartials] = {1, 1, 1, 1, 1, 1};
static float phase[kPartials];
static float env = 0.f, pitchEnv = 0.f, level = 0.f;
static float dcX = 0.f, dcY = 0.f;
static float decayCoef = 0.9995f;
static float velTimbre = 1.f, noiseEnv = 0.f, noiseEnvCoef = 0.999f;
static volatile float popTarget = 0.f, attackNoiseTarget = 0.f;
static float popEnv = 0.f, anoiseEnv = 0.f, popCoef = 0.9f, anoiseCoef = 0.9f;
static float osInvSr = 1.f / 96000.f;
static int stage = 0;
static float pitchEnvCoef = 0.999f;

static float baseline = 0.5f;
static bool armed = false;
static int refractoryMs = 0;
static int settleMs = 100;

static bool shift = false, prevShift = false;
static float primary[4], secondary[4];
static bool caught[4];
static float lastPot[4];

static uint32_t flashTime = 0, selectTime = 0;
static float flashVel = 0.f;

static inline float Saturate(float x) {
  if (x < -1.5f) return -1.f;
  if (x > 1.5f) return 1.f;
  return x - 0.14814f * x * x * x;
}

static void Fire(float vel) {
  pendingVel = vel;
  pendingTrig = 1;
  flashTime = System::GetNow();
  flashVel = vel;
}

struct Biquad {
  float b0, b1, b2, a1, a2, z1, z2;
  inline float P(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};
static Biquad dec0, dec1;

static void SetLP(Biquad& q, float fc, float fs, float qf) {
  float w = 6.2831853f * fc / fs, c = cosf(w), s = sinf(w);
  float al = s / (2.f * qf), a0 = 1.f + al;
  q.b0 = (1.f - c) * 0.5f / a0;
  q.b1 = (1.f - c) / a0;
  q.b2 = q.b0;
  q.a1 = -2.f * c / a0;
  q.a2 = (1.f - al) / a0;
  q.z1 = 0.f;
  q.z2 = 0.f;
}

static float Render() {
  if (pendingTrig) {
    pendingTrig = 0;
    level = 0.30f + 0.70f * pendingVel;
    velTimbre = 0.75f + 0.5f * pendingVel;
    env = 0.f;
    stage = 0;
    pitchEnv = 1.f;
    noiseEnv = 1.f;
    popEnv = popTarget;
    anoiseEnv = attackNoiseTarget;
    for (int k = 0; k < kPartials; k++) {
      phase[k] = 0.f;
      rdec[k] = 1.f;
    }
  }

  if (stage == 0) {
    env += attackInc;
    if (env >= 1.f) {
      env = 1.f;
      stage = 1;
    }
  } else {
    env *= decayCoef;
  }
  noiseEnv *= noiseEnvCoef;
  popEnv *= popCoef;
  anoiseEnv *= anoiseCoef;

  freq += kFreqSmooth * (freqTarget - freq);
  morph += kSmooth * (morphTarget - morph);
  fold += kSmooth * (foldTarget - fold);
  noiseAmt += kSmooth * (noiseTarget - noiseAmt);
  decayCoef += kSmooth * (decayCoefTarget - decayCoef);
  for (int k = 0; k < kPartials; k++) {
    ratio[k] += kSmooth * (ratioTarget[k] - ratio[k]);
    amp[k] += kSmooth * (ampTarget[k] - amp[k]);
    relCoef[k] += kSmooth * (relCoefTarget[k] - relCoef[k]);
    rdec[k] *= relCoef[k];
  }
  modRatio += kSmooth * (modRatioTarget - modRatio);
  fmIndex += kSmooth * (indexTarget - fmIndex);

  float pmul = 1.f;
  if (mode == LIQUID) {
    pitchEnv *= pitchEnvCoef;
    pmul = 1.f + kPitchEnvAmt * pitchEnv;
  }
  float f0 = freq * pmul;

  float halfSr = 0.5f * sampleRate;
  float nyLo = 0.45f * sampleRate;
  float nyRange = 0.05f * sampleRate;
  float osr = sampleRate * kOS;

  float fv = fold * velTimbre;
  if (fv > 1.2f) fv = 1.2f;
  float thrBase = 0.10f + 1.5f * (1.f - Clamp(fv / 0.75f, 0.f, 1.f));
  float thr = thrBase * (1.f + (1.f - env) * 2.0f * Clamp(fold, 0.f, 1.f));
  float comp = sqrtf(1.6f / thrBase);
  if (comp > 4.f) comp = 4.f;

  float dec = 0.f;
  for (int o = 0; o < kOS; o++) {
    float sig = 0.f;
    if (mode == METAL) {
      float idx = fmIndex;
      float idxN = fmIndex * 0.7f;
      for (int c = 0; c < 2; c++) {
        int ci = c * 3, mi = c * 3 + 1, ni = c * 3 + 2;
        float cf = f0 * (c == 0 ? ratio[0] : ratio[2]);
        float mf = cf * modRatio;
        float nf = mf * modRatio;
        phase[ci] += cf * osInvSr;
        if (phase[ci] >= 1.f) phase[ci] -= 1.f;
        phase[mi] += mf * osInvSr;
        if (phase[mi] >= 1.f) phase[mi] -= 1.f;
        phase[ni] += nf * osInvSr;
        if (phase[ni] >= 1.f) phase[ni] -= 1.f;
        float g = Clamp((halfSr - cf) / nyRange, 0.f, 1.f);
        float capN = 0.5f * osr / (nf + 1.f);
        float in = idxN < capN ? idxN : capN;
        float opN = FastSin(phase[ni]);
        float opM = FastSin(phase[mi] + in * opN);
        float ph = phase[ci] + idx * opM;
        ph -= floorf(ph);
        float aC = c == 0 ? 1.0f : 0.7f;
        float dC = c == 0 ? rdec[0] : rdec[1];
        sig += aC * dC * g * MorphWave(ph, cf * osInvSr, morph);
      }
      sig *= 0.8f;
    } else {
      for (int k = 0; k < kPartials; k++) {
        float fk = f0 * ratio[k];
        float dt = fk * osInvSr;
        if (dt > 0.5f) dt = 0.5f;
        phase[k] += dt;
        if (phase[k] >= 1.f) phase[k] -= 1.f;
        float g = fk <= nyLo ? 1.f
                             : Clamp((halfSr - fk) / nyRange, 0.f, 1.f);
        sig += amp[k] * rdec[k] * g * MorphWave(phase[k], dt, morph);
      }
    }

    float noiseGain = noiseAmt * noiseEnv + anoiseEnv * 0.6f;
    if (noiseGain > 0.001f) sig += Noise() * noiseGain;

    float x = sig / thr;
    x = x - 4.f * floorf(0.25f * x + 0.25f);
    float ax = fabsf(x);
    float rf = ax > 1.f ? (2.f - ax) * (x < 0.f ? -1.f : 1.f) : x;
    sig = rf * thr * comp;

    if (fold > 0.75f) {
      float pulse = phase[0] < 0.5f ? 0.5f : -0.5f;
      sig += (fold - 0.75f) * 2.4f * pulse;
    }

    sig += popEnv * 0.6f;

    dec = dec1.P(dec0.P(sig));
  }

  float pre = dec * env * level * kModeGain[mode];
  dcY = pre - dcX + 0.999f * dcY;
  dcX = pre;
  return Saturate(dcY);
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  for (size_t i = 0; i < size; i++) {
    float s = Render();
    out[0][i] = s;
    out[1][i] = s;
  }
}

static void PollTrigger(float cv) {
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

static void TrackPots() {
  float pot[4] = {hw.GetPot(POT_1), hw.GetPot(POT_2), hw.GetPot(POT_3),
                  hw.GetPot(POT_4)};
  float* layer = shift ? secondary : primary;
  if (shift != prevShift) {
    for (int i = 0; i < 4; i++) caught[i] = fabsf(pot[i] - layer[i]) < 0.02f;
    prevShift = shift;
  }
  for (int i = 0; i < 4; i++) {
    if (!caught[i]) {
      bool crossed = (pot[i] - layer[i]) * (lastPot[i] - layer[i]) < 0.f;
      if (fabsf(pot[i] - layer[i]) < 0.02f || crossed) caught[i] = true;
    }
    if (caught[i]) layer[i] = pot[i];
    lastPot[i] = pot[i];
  }
}

static void ApplyControls() {
  float base = Expo(primary[0], 24.f, 3000.f);
  float oct = (hw.GetCv(CV_2) - 0.5f) * 4.f;
  freqTarget = base * exp2f(oct);
  morphTarget = primary[1];
  foldTarget = primary[2];

  float decayTime = Expo(primary[3], 0.02f, 4.f);
  decayCoefTarget = expf(-1.f / (decayTime * sampleRate));

  float spread = secondary[0];
  float harm = secondary[1];

  float atk = secondary[2];
  if (atk <= 0.5f) {
    attackInc = 1.f / (0.0005f * sampleRate);
    popTarget = 1.f;
    attackNoiseTarget = (0.5f - atk) * 2.f;
  } else {
    float slow = (atk - 0.5f) * 2.f;
    attackInc = 1.f / (Expo(slow, 0.0005f, 0.15f) * sampleRate);
    popTarget = 1.f - slow;
    attackNoiseTarget = 0.f;
  }
  noiseTarget = secondary[3] * 0.5f;

  float second = Clamp(harm * 4.f, 0.f, 1.f);
  second = second * second * (3.f - 2.f * second);
  float upper = Clamp((harm - 0.35f) / 0.65f, 0.f, 1.f);
  upper = upper * upper * (3.f - 2.f * upper);

  float a[kPartials];
  a[0] = 1.f;
  a[1] = second;
  for (int k = 2; k < kPartials; k++)
    a[k] = upper * powf(static_cast<float>(k + 1), -0.8f);

  float power = 0.f;
  for (int k = 0; k < kPartials; k++) {
    ratioTarget[k] = kHarmRatio[k] + spread * (kPrimeRatio[k] - kHarmRatio[k]);
    power += a[k] * a[k];
  }
  float inv = 1.f / sqrtf(power);
  for (int k = 0; k < kPartials; k++) ampTarget[k] = a[k] * inv;

  float lenUp = 0.12f + 0.83f * Clamp((harm - 0.08f) / 0.7f, 0.f, 1.f);
  float invTau = 1.f / (decayTime * sampleRate);
  relCoefTarget[0] = 1.f;
  for (int k = 1; k < kPartials; k++) {
    float mf = lenUp / (1.f + 0.35f * (k - 1));
    relCoefTarget[k] = expf(invTau * (1.f - 1.f / mf));
  }

  modRatioTarget = 1.f + 2.5f * spread + 0.5f * harm;
  indexTarget = 0.5f + harm * 7.f + primary[2] * 1.5f;

  pitchEnvCoef = expf(-1.f / (0.045f * sampleRate));
}

static void UpdateButtons() {
  if (hw.button[BUTTON_1].FallingEdge()) {
    mode = (mode + 1) % kNumModes;
    selectTime = System::GetNow();
    Fire(kAuditionVel);
  }
  shift = hw.button[BUTTON_2].Pressed();
  if (shift) selectTime = System::GetNow();
}

static void UpdateLed() {
  uint32_t now = System::GetNow();
  float hue = kModeHue[mode];

  if (now - flashTime < kFlashMs) {
    float v = flashVel * (1.f - static_cast<float>(now - flashTime) / kFlashMs);
    hw.led.SetHsv(hue, shift ? 0.35f : 0.9f, 0.15f + 0.85f * v);
    return;
  }
  if (now - selectTime < kSelectMs) {
    hw.led.SetHsv(hue, shift ? 0.3f : 0.85f, shift ? 0.5f : 0.4f);
    return;
  }
  float breath = 0.5f - 0.5f * cosf(TWOPI_F * (now % 3600) / 3600.f);
  hw.led.SetHsv(hue, 0.8f, 0.05f + 0.10f * breath);
}

static void RunBootSweep() {
  static const float steps[][3] = {
      {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}};
  for (size_t i = 0; i < 4; i++) {
    hw.led.Set(steps[i][0], steps[i][1], steps[i][2]);
    hw.led.Update();
    System::Delay(150);
  }
}

int main(void) {
  hw.Init();
  sampleRate = hw.SampleRate();

  osInvSr = 1.f / (sampleRate * kOS);
  noiseEnvCoef = expf(-1.f / (0.025f * sampleRate));
  popCoef = expf(-1.f / (0.0015f * sampleRate));
  anoiseCoef = expf(-1.f / (0.006f * sampleRate));
  SetLP(dec0, 19000.f, sampleRate * kOS, 0.54120f);
  SetLP(dec1, 19000.f, sampleRate * kOS, 1.30656f);

  for (int k = 0; k < kPartials; k++) {
    ratio[k] = k + 1;
    amp[k] = k == 0 ? 1.f : 0.f;
    phase[k] = 0.f;
  }
  for (int i = 0; i < 4; i++) {
    primary[i] = hw.GetPot(static_cast<Pot>(i == 0   ? POT_1
                                            : i == 1 ? POT_2
                                            : i == 2 ? POT_3
                                                     : POT_4));
    secondary[i] = kSecondaryBoot[i];
    caught[i] = true;
    lastPot[i] = primary[i];
  }
  (void)kPrimaryBoot;
  ApplyControls();

  RunBootSweep();
  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();
    UpdateButtons();
    TrackPots();
    PollTrigger(hw.GetCv(CV_1));
    ApplyControls();
    UpdateLed();
    System::Delay(1);
  }
}
