#include <cmath>

#include "../../lib/carciofo.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace carciofo;

/*
 * Tocco. A channel strip for two mono inputs, always live and always in the
 * same order:
 *
 *   in 1 -> level/saturation -> eq -> dj filter -> ducker -+-> limiter -> L + R
 *   in 2 -> level -+---------------(sidechain)-----^       |
 *                  +-> saturation -> eq -> mix ------------+
 *
 * Each effect is a layer, the four pots reach that layer's parameters and
 * nothing else. B2 steps to the next layer, B1 to the previous one.
 *
 *   layer       P1          P2          P3          P4
 *   level       in 1 level  in 1 drive  in 2 level  in 2 drive
 *   eq          high gain   mid gain    mid freq    low gain
 *   filter      lowpass     lp reso     highpass    hp reso
 *   ducker      threshold   amount      release     in 2 mix
 *
 * Switching a layer strands the pots away from the values stored underneath,
 * so a pot only takes hold once it has been turned back through its stored
 * value, the same soft pickup quattro uses. Nothing jumps.
 *
 * Every pot value and the current layer are saved to the QSPI flash a few
 * seconds after the last change and come back at power up. Holding both
 * buttons while powering up resets everything to the neutral patch.
 */

enum Fx { FX_LEVEL = 0, FX_EQ, FX_FILTER, FX_DUCKER, kNumFx };
static const int kNumPots = 4;

// One pole coefficient, roughly 40 ms at 48 kHz, so a level step glides in.
static const float kSmoothing = 0.0005f;

static const float kCatchEps = 0.02f;
static const float kMoveEps = 0.004f;
static const uint32_t kEditHoldMs = 1100;

// Saving waits this long after the last change so a turn costs one write, not
// hundreds, and only a change bigger than the pot jitter counts as one.
static const uint32_t kSaveDelayMs = 3000;
static const float kSaveEps = 0.01f;

// Far up the 8 MB QSPI so it stays clear of programs the Daisy bootloader puts
// at the start. Bump the version whenever Settings changes shape.
static const uint32_t kStorageOffset = 0x700000;
static const uint32_t kSettingsVersion = 1;

// The EQ's fixed shape: shelves at 100 Hz and 8 kHz, a mid bell of Q 1 swept
// from 150 Hz to 8 kHz, every band +-15 dB with flat at noon.
static const float kLowShelf = 100.f;
static const float kHighShelf = 8000.f;
static const float kMidQ = 1.f;
static const float kEqRangeDb = 15.f;

// The DJ filters are both open at zero and close as the pot turns up, CV1
// closing the lowpass and CV2 the highpass on top of their pots. Each fades in
// over this much travel off zero, so an open filter is a true bypass.
static const float kFilterFade = 0.06f;

// The ducker follows in 2 with a fixed 5 ms attack, quick enough to catch a
// kick yet slow enough that the gain never steps. The threshold pot sets the
// level of in 2 that ducks by the full amount, half scale down to -34 dB.
static const float kDuckAttackMs = 5.f;

// The limiter scales its output by 0.7, this undoes it so anything below
// that ceiling passes at unity and only the peaks are caught.
static const float kLimiterGain = 1.f / 0.7f;

// Identity colour per layer.
static const float kFxRgb[kNumFx][3] = {
    {1.00f, 0.15f, 0.00f},  // level, red
    {1.00f, 0.70f, 0.00f},  // eq, amber
    {0.00f, 0.55f, 1.00f},  // filter, blue
    {0.55f, 0.10f, 1.00f},  // ducker, violet
};

static Carciofo hw;

// Written by the control loop, read by the audio callback, same lockless
// handoff as the other firmwares.
static float levelTarget[2] = {1.f, 1.f};
static float driveTarget[2];

static float eqTarget[4];  // low dB, mid dB, mid Hz, high dB
static float lowTarget;
static float highTarget;
static float lowResTarget;
static float highResTarget;
static float duckTarget;
static float senseTarget = 4.f;
static float releaseCoef;
static float sideMixTarget = 1.f;

static float level[2] = {1.f, 1.f};
static float drive[2];
static float eq[4];
static float lowPos;
static float highPos;
static float lowWet;
static float highWet;
static float duck;
static float sense = 4.f;
static float sideMix = 1.f;
static float envelope;
static float attackCoef;

static float knob[kNumFx][kNumPots];
static bool caught[kNumFx][kNumPots];
static float lastRaw[kNumPots];

static int layer = FX_LEVEL;

struct Settings {
  uint32_t version;
  int32_t layer;
  float knob[kNumFx][kNumPots];

  bool operator!=(const Settings& o) const {
    if (version != o.version || layer != o.layer) return true;
    for (int f = 0; f < kNumFx; f++)
      for (int p = 0; p < kNumPots; p++)
        if (fabsf(knob[f][p] - o.knob[f][p]) > kSaveEps) return true;
    return false;
  }
};

static PersistentStorage<Settings> storage(hw.seed.qspi);

// The QSPI window is cached and PersistentStorage only invalidates it when the
// program runs outside internal flash, so do it here before it compares.
static void InvalidateStorageCache() {
  dsy_dma_invalidate_cache_for_buffer(
      static_cast<uint8_t*>(hw.seed.qspi.GetData(kStorageOffset)),
      sizeof(Settings) + sizeof(uint32_t));
}
static int activePot;
static uint32_t lastMoveTime;
static uint32_t catchFlashTime;
static uint32_t saveFlashTime;

static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static inline float Expo(float t, float lo, float hi) {
  return lo * powf(hi / lo, t);
}

// RBJ cookbook biquad, transposed direct form II.
struct Biquad {
  float b0 = 1.f, b1, b2, a1, a2, z1, z2;

  float Process(float x) {
    float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }

  enum Shape { LOW_SHELF, PEAK, HIGH_SHELF };

  void Set(Shape shape, float hz, float db, float q, float sampleRate) {
    float A = powf(10.f, db / 40.f);
    float w = TWOPI_F * hz / sampleRate;
    float cw = cosf(w);
    float alpha = sinf(w) / (2.f * q);
    float n0, n1, n2, d0, d1, d2;
    if (shape == PEAK) {
      n0 = 1.f + alpha * A;
      n1 = -2.f * cw;
      n2 = 1.f - alpha * A;
      d0 = 1.f + alpha / A;
      d1 = -2.f * cw;
      d2 = 1.f - alpha / A;
    } else {
      // A high shelf is the low shelf with the sign of cos flipped.
      float s = shape == LOW_SHELF ? 1.f : -1.f;
      float k = 2.f * sqrtf(A) * alpha;
      n0 = A * ((A + 1.f) - s * (A - 1.f) * cw + k);
      n1 = s * 2.f * A * ((A - 1.f) - s * (A + 1.f) * cw);
      n2 = A * ((A + 1.f) - s * (A - 1.f) * cw - k);
      d0 = (A + 1.f) + s * (A - 1.f) * cw + k;
      d1 = -s * 2.f * ((A - 1.f) + s * (A + 1.f) * cw);
      d2 = (A + 1.f) + s * (A - 1.f) * cw - k;
    }
    b0 = n0 / d0;
    b1 = n1 / d0;
    b2 = n2 / d0;
    a1 = d1 / d0;
    a2 = d2 / d0;
  }
};

// One chain per input, both running the same settings.
static Biquad eqLow[2], eqMid[2], eqHigh[2];
static Svf lowpass, highpass;
static Limiter limiter;

// Up to 26 dB into a tanh, divided back down so a full scale peak stays put,
// faded in by the amount so zero is a clean thru with no step.
static inline float Saturate(float x, float amount) {
  if (amount < 0.001f) return x;
  float gain = powf(20.f, amount);
  return Lerp(x, tanhf(x * gain) / tanhf(gain), amount);
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  // The EQ glides at block rate, coefficients redone from the smoothed values
  // once per block so a turned pot never clicks.
  float blockSmooth = kSmoothing * size;
  for (int b = 0; b < 4; b++) eq[b] += blockSmooth * (eqTarget[b] - eq[b]);
  float sr = hw.SampleRate();
  for (int c = 0; c < 2; c++) {
    eqLow[c].Set(Biquad::LOW_SHELF, kLowShelf, eq[0], 0.707f, sr);
    eqMid[c].Set(Biquad::PEAK, eq[2], eq[1], kMidQ, sr);
    eqHigh[c].Set(Biquad::HIGH_SHELF, kHighShelf, eq[3], 0.707f, sr);
  }

  lowPos += blockSmooth * (lowTarget - lowPos);
  highPos += blockSmooth * (highTarget - highPos);
  lowWet = Clamp(lowPos / kFilterFade, 0.f, 1.f);
  highWet = Clamp(highPos / kFilterFade, 0.f, 1.f);
  // The Svf goes unstable above about 13 kHz at high resonance, so the
  // lowpass tops out at 12 kHz, still well past audible once faded out.
  float lowFreq = Expo(lowPos, 12000.f, 60.f);
  float highFreq = Expo(highPos, 20.f, 6000.f);
  lowpass.SetFreq(lowFreq);
  highpass.SetFreq(highFreq);
  lowpass.SetRes(lowResTarget);
  highpass.SetRes(highResTarget);

  for (size_t i = 0; i < size; i++) {
    for (int c = 0; c < 2; c++) {
      level[c] += kSmoothing * (levelTarget[c] - level[c]);
      drive[c] += kSmoothing * (driveTarget[c] - drive[c]);
    }

    float main = Saturate(in[0][i] * level[0], drive[0]);
    // The ducker hears in 2 before its drive, so the drive only colours what
    // is mixed in and never changes how hard the sidechain hits.
    float side = in[1][i] * level[1];

    main = eqHigh[0].Process(eqMid[0].Process(eqLow[0].Process(main)));

    lowpass.Process(main);
    main = Lerp(main, lowpass.Low(), lowWet);
    highpass.Process(main);
    main = Lerp(main, highpass.High(), highWet);

    duck += kSmoothing * (duckTarget - duck);
    sense += kSmoothing * (senseTarget - sense);
    sideMix += kSmoothing * (sideMixTarget - sideMix);
    float rect = fabsf(side);
    envelope += (rect > envelope ? attackCoef : releaseCoef) * (rect - envelope);
    main *= 1.f - duck * Clamp(envelope * sense, 0.f, 1.f);

    float sideOut = Saturate(side, drive[1]);
    sideOut = eqHigh[1].Process(eqMid[1].Process(eqLow[1].Process(sideOut)));
    out[0][i] = main + sideOut * sideMix;

    // Last line of defence: a filter that ever blows up is reset on the spot
    // instead of leaving the output silent until the next reset.
    if (!std::isfinite(out[0][i])) {
      for (Svf* f : {&lowpass, &highpass}) f->Init(sr);
      lowpass.SetFreq(lowFreq);
      highpass.SetFreq(highFreq);
      lowpass.SetRes(lowResTarget);
      highpass.SetRes(highResTarget);
      for (int c = 0; c < 2; c++)
        for (Biquad* b : {&eqLow[c], &eqMid[c], &eqHigh[c]}) b->z1 = b->z2 = 0.f;
      envelope = 0.f;
      out[0][i] = 0.f;
    }
  }

  limiter.ProcessBlock(out[0], size, kLimiterGain);
  for (size_t i = 0; i < size; i++) out[1][i] = out[0][i];
}

/* Soft pickup for one pot on the active layer, returns how far it moved. */
static float TrackPot(int pot, float raw) {
  float stored = knob[layer][pot];
  if (!caught[layer][pot]) {
    bool crossed = (raw - stored) * (lastRaw[pot] - stored) < 0.f;
    if (fabsf(raw - stored) < kCatchEps || crossed) {
      caught[layer][pot] = true;
      catchFlashTime = System::GetNow();
    }
  }
  if (caught[layer][pot]) knob[layer][pot] = raw;
  float moved = fabsf(raw - lastRaw[pot]);
  lastRaw[pot] = raw;
  return moved;
}

static void ApplyControls() {
  // Squared taper, unity at noon, +12 dB fully up.
  for (int c = 0; c < 2; c++) {
    float l = knob[FX_LEVEL][c * 2];
    levelTarget[c] = 4.f * l * l;
    driveTarget[c] = knob[FX_LEVEL][c * 2 + 1];
  }

  const float* k = knob[FX_EQ];
  eqTarget[0] = (k[3] * 2.f - 1.f) * kEqRangeDb;
  eqTarget[1] = (k[1] * 2.f - 1.f) * kEqRangeDb;
  eqTarget[2] = Expo(k[2], 150.f, 8000.f);
  eqTarget[3] = (k[0] * 2.f - 1.f) * kEqRangeDb;

  // Resonance kept shy of 1 so the filters sing without self oscillating.
  const float* f = knob[FX_FILTER];
  lowTarget = Clamp(f[0] + hw.GetCv(CV_1), 0.f, 1.f);
  lowResTarget = 0.9f * f[1];
  highTarget = Clamp(f[2] + hw.GetCv(CV_2), 0.f, 1.f);
  highResTarget = 0.9f * f[3];

  const float* d = knob[FX_DUCKER];
  senseTarget = Expo(d[0], 2.f, 50.f);
  duckTarget = d[1];
  float releaseMs = Expo(d[2], 30.f, 1000.f);
  releaseCoef = 1.f - expf(-1000.f / (releaseMs * hw.SampleRate()));
  sideMixTarget = d[3] * d[3];
}

static void UpdateLed() {
  uint32_t now = System::GetNow();
  const float* rgb = kFxRgb[layer];

  if (now - catchFlashTime < 110) {
    hw.led.Set(1.f, 1.f, 1.f);
    return;
  }

  // A short green blip confirms the patch went to flash.
  if (saveFlashTime && now - saveFlashTime < 250) {
    hw.led.Set(0.f, 0.6f, 0.f);
    return;
  }

  // A pot turned in the last moment shows its value, blinking until caught.
  if (now - lastMoveTime < kEditHoldMs) {
    float value = 0.12f + 0.88f * knob[layer][activePot];
    if (!caught[layer][activePot] && (now % 260) >= 130) value *= 0.12f;
    hw.led.Set(rgb[0] * value, rgb[1] * value, rgb[2] * value);
    return;
  }

  // Idle, the layer colour breathing so you always know where you are.
  float breath = 0.5f + 0.5f * sinf(TWOPI_F * (now % 3000) / 3000.f);
  float v = 0.10f + 0.25f * breath;
  hw.led.Set(rgb[0] * v, rgb[1] * v, rgb[2] * v);
}

static void EnterLayer(int next) {
  layer = (next + kNumFx) % kNumFx;
  // Any pot already sitting on its stored value is caught for free.
  for (int p = 0; p < kNumPots; p++) {
    caught[layer][p] =
        fabsf(hw.GetPot(static_cast<Pot>(p)) - knob[layer][p]) < kCatchEps;
  }
}

/* Writes the patch once it has stopped changing. The write blocks the control
   loop for a moment, the audio keeps running underneath. */
static void SaveWhenSettled() {
  static Settings pending;
  static uint32_t lastChange;
  Settings now;
  now.version = kSettingsVersion;
  now.layer = layer;
  for (int f = 0; f < kNumFx; f++)
    for (int p = 0; p < kNumPots; p++) now.knob[f][p] = knob[f][p];

  if (now != pending) {
    pending = now;
    lastChange = System::GetNow();
  }
  if (System::GetNow() - lastChange > kSaveDelayMs &&
      pending != storage.GetSettings()) {
    storage.GetSettings() = pending;
    InvalidateStorageCache();
    storage.Save();
    saveFlashTime = System::GetNow();
  }
}

int main(void) {
  hw.Init();
  lowpass.Init(hw.SampleRate());
  highpass.Init(hw.SampleRate());
  limiter.Init();
  attackCoef = 1.f - expf(-1000.f / (kDuckAttackMs * hw.SampleRate()));

  // The neutral patch: unity level, no drive, flat eq, filters open, no duck.
  static const Settings kDefaults = {kSettingsVersion,
                                     FX_LEVEL,
                                     {
                                         {0.50f, 0.00f, 0.50f, 0.00f},  // level
                                         {0.50f, 0.50f, 0.50f, 0.50f},  // eq
                                         {0.00f, 0.00f, 0.00f, 0.00f},  // filter
                                         {0.25f, 0.00f, 0.30f, 1.00f},  // ducker
                                     }};
  InvalidateStorageCache();
  storage.Init(kDefaults, kStorageOffset);

  // Both buttons held at power up, or a save from an older layout, start over.
  // The pins get a moment to settle and a full debounce, so a slow pull up at
  // power on never reads as a press.
  System::Delay(100);
  for (int i = 0; i < 20; i++) {
    hw.button[BUTTON_1].Debounce();
    hw.button[BUTTON_2].Debounce();
    System::Delay(1);
  }
  bool reset = hw.button[BUTTON_1].Pressed() && hw.button[BUTTON_2].Pressed();
  if (reset || storage.GetSettings().version != kSettingsVersion)
    storage.RestoreDefaults();
  if (reset) {
    hw.led.Set(1.f, 1.f, 1.f);
    hw.led.Update();
    System::Delay(400);
  }

  // The flash is not trusted blindly, anything out of range falls back.
  const Settings& saved = storage.GetSettings();
  for (int f = 0; f < kNumFx; f++) {
    for (int p = 0; p < kNumPots; p++) {
      float v = saved.knob[f][p];
      knob[f][p] = (v >= 0.f && v <= 1.f) ? v : kDefaults.knob[f][p];
    }
  }
  for (int p = 0; p < kNumPots; p++)
    lastRaw[p] = hw.GetPot(static_cast<Pot>(p));
  // Every layer waits to be picked up, so the saved patch is what you hear.
  EnterLayer(saved.layer >= 0 && saved.layer < kNumFx ? saved.layer : FX_LEVEL);
  ApplyControls();
  for (int c = 0; c < 2; c++) {
    level[c] = levelTarget[c];
    drive[c] = driveTarget[c];
  }
  for (int b = 0; b < 4; b++) eq[b] = eqTarget[b];
  lowPos = lowTarget;
  highPos = highTarget;
  duck = duckTarget;
  sense = senseTarget;
  sideMix = sideMixTarget;

  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();

    if (hw.button[BUTTON_2].RisingEdge()) EnterLayer(layer + 1);
    if (hw.button[BUTTON_1].RisingEdge()) EnterLayer(layer - 1);

    float bestMove = kMoveEps;
    int moved = -1;
    for (int p = 0; p < kNumPots; p++) {
      float m = TrackPot(p, hw.GetPot(static_cast<Pot>(p)));
      if (m > bestMove) {
        bestMove = m;
        moved = p;
      }
    }
    if (moved >= 0) {
      activePot = moved;
      lastMoveTime = System::GetNow();
    }

    ApplyControls();
    UpdateLed();
    SaveWhenSettled();
    System::Delay(1);
  }
}
