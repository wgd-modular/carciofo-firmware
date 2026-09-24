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

#define MAX_DELAY static_cast<size_t>(48000 * 16.0f)
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS dell;
static DelayLine<float, MAX_DELAY> DSY_SDRAM_BSS delr;
static Chorus chorus;
static Tremolo tremL;
static Tremolo tremR;
static Oscillator flutter;

static const float kTrigRise = 0.20f;
static const float kTrigRearm = 0.07f;
static const uint32_t kMinIntervalMs = 40;
static const uint32_t kMaxIntervalMs = 8000;
static const uint32_t kTapMs = 260;
static const uint32_t kLongPressMs = 400;
static const uint32_t kBeatFlashMs = 90;
static const uint32_t kConfirmMs = 220;

static Carciofo hw;

// Shared with the audio callback. The main loop owns every control; the
// callback only renders.
static volatile float sFeedback = 0.f;
static volatile float sDryAmp = 0.5f;
static volatile float sWetAmp = 0.5f;
static volatile float sTargetL = 24000.f;
static volatile float sTargetR = 24000.f;
static volatile float sSmoodge = 0.f;
static volatile float sGain = 0.5f;
static volatile bool sFrozen = false;

// Tempo state (main loop).
static float delayTimeSecs = 0.5f;
static float divisor = 1.f;
static uint32_t lastBeat = 0;
static uint32_t ivBuf[3] = {0, 0, 0};
static int ivIdx = 0;
static int ivCount = 0;

// CV1 clock edge detector.
static float clockBaseline = 0.5f;
static bool clockArmed = false;
static int bootSettle = 200;

// Buttons.
static bool frozen = false;
static bool cv2ControlsFeedback = true;
static bool b2LongHandled = false;
static uint32_t b2PressStart = 0;

// LED.
static float ledPhaseMs = 0.f;
static uint32_t beatFlash = 0;
static uint32_t confirmFlash = 0;
static uint32_t prevNow = 0;

static void RegisterBeat(uint32_t now) {
  if (lastBeat != 0) {
    uint32_t iv = now - lastBeat;
    if (iv >= kMinIntervalMs && iv <= kMaxIntervalMs) {
      ivBuf[ivIdx] = iv;
      ivIdx = (ivIdx + 1) % 3;
      if (ivCount < 3) ivCount++;
      uint32_t sum = 0;
      for (int i = 0; i < ivCount; i++) sum += ivBuf[i];
      delayTimeSecs = (static_cast<float>(sum) / ivCount) / 1000.f;
    } else if (iv > kMaxIntervalMs) {
      ivCount = 0;
      ivIdx = 0;
    }
  }
  lastBeat = now;
  beatFlash = now;
}

static void PollClock(uint32_t now) {
  float cv = hw.GetCv(CV_1);
  if (bootSettle > 0) {
    bootSettle--;
    clockBaseline += 0.2f * (cv - clockBaseline);
    return;
  }
  if (!clockArmed) {
    clockBaseline += 0.02f * (cv - clockBaseline);
    if (cv > clockBaseline + kTrigRise) {
      clockArmed = true;
      RegisterBeat(now);
    }
  } else if (cv < clockBaseline + kTrigRearm) {
    clockArmed = false;
  }
}

static void PollButtons(uint32_t now) {
  if (hw.button[BUTTON_1].RisingEdge()) RegisterBeat(now);

  if (hw.button[BUTTON_2].RisingEdge()) {
    b2PressStart = now;
    b2LongHandled = false;
  }
  if (hw.button[BUTTON_2].Pressed() && !b2LongHandled &&
      hw.button[BUTTON_2].TimeHeldMs() >= kLongPressMs) {
    cv2ControlsFeedback = !cv2ControlsFeedback;
    b2LongHandled = true;
    confirmFlash = now;
  }
  if (hw.button[BUTTON_2].FallingEdge() && !b2LongHandled &&
      now - b2PressStart < kTapMs) {
    frozen = !frozen;
    confirmFlash = now;
  }
}

static void DivisorFromPot(float p3) {
  if (p3 <= 0.05f) {
    divisor = 8.f;
  } else if (p3 <= 0.15f) {
    divisor = 4.f;
  } else if (p3 <= 0.25f) {
    divisor = 3.f;
  } else if (p3 <= 0.35f) {
    divisor = 2.f;
  } else if (p3 <= 0.45f) {
    divisor = 1.5f;
  } else if (p3 <= 0.55f) {
    divisor = 1.f;
  } else if (p3 <= 0.65f) {
    divisor = 0.5f;
  } else if (p3 <= 0.75f) {
    divisor = 0.33f;
  } else if (p3 <= 0.85f) {
    divisor = 0.25f;
  } else if (p3 <= 0.95f) {
    divisor = 0.2f;
  } else {
    divisor = 0.125f;
  }
}

static void UpdateControls() {
  float p1 = hw.GetPot(POT_1);
  float p2 = hw.GetPot(POT_2);
  float p3 = hw.GetPot(POT_3);
  float p4 = hw.GetPot(POT_4);
  float cv2 = hw.GetCv(CV_2) - 0.5f;  // bipolar around the idle reading
  if (fabsf(cv2) < 0.02f) cv2 = 0.f;

  float feedback;
  float smoodge;
  if (cv2ControlsFeedback) {
    feedback = Clamp(p1 + cv2, 0.f, 1.f);
    smoodge = p4;
  } else {
    feedback = p1;
    smoodge = Clamp(p4 + cv2, 0.f, 1.f);
  }

  DivisorFromPot(p3);

  float base = (delayTimeSecs * 48000.f) / divisor;
  float wobble = flutter.Process();
  flutter.SetAmp(9.0f * smoodge);
  float mod = (20.f * wobble) * (smoodge * smoodge);
  float targetL = base + mod;
  float targetR = (base * (1.f - smoodge / 14.f)) + mod;
  targetL = Clamp(targetL, 1.f, static_cast<float>(MAX_DELAY - 1));
  targetR = Clamp(targetR, 1.f, static_cast<float>(MAX_DELAY - 1));

  chorus.SetLfoFreq(smoodge);
  chorus.SetLfoDepth(0.2f);
  chorus.SetFeedback(smoodge * smoodge * 0.5f);
  tremL.SetDepth(smoodge * 0.61f);
  tremR.SetDepth(smoodge * 0.60f);

  sFeedback = feedback;
  sSmoodge = smoodge;
  sGain = 0.5f + smoodge * 0.4f;
  sTargetL = targetL;
  sTargetR = targetR;
  sWetAmp = p2;
  sDryAmp = 1.f - p2;
  sFrozen = frozen;
}

static float ledHue = 0.33f;
static float ledSat = 1.f;

static void UpdateLed(uint32_t now, float dtMs) {
  float periodMs = Clamp((delayTimeSecs / divisor) * 1000.f, 30.f, 8000.f);
  ledPhaseMs += dtMs;
  if (ledPhaseMs >= periodMs) {
    ledPhaseMs -= periodMs;
    beatFlash = now;
  }

  float t = (log2f(divisor) + 3.f) / 6.f;
  t = Clamp(t, 0.f, 1.f);
  ledHue = 0.66f - 0.60f * t;  // long echoes blue, short echoes red
  ledSat = 1.f - 0.5f * sSmoodge;

  if (now - confirmFlash < kConfirmMs) {
    float pulse = 0.5f + 0.5f * cosf(TWOPI_F * (now % 110) / 110.f);
    float hue = cv2ControlsFeedback ? 0.33f : 0.08f;
    hw.led.SetHsv(hue, 0.7f, 0.35f + 0.5f * pulse);
    return;
  }
  if (frozen) {
    float pulse = 0.5f - 0.5f * cosf(TWOPI_F * (now % 1400) / 1400.f);
    hw.led.SetHsv(0.85f, 0.8f, 0.5f + 0.45f * pulse);
    return;
  }

  float breath = 0.5f - 0.5f * cosf(TWOPI_F * (now % 4000) / 4000.f);
  float v = 0.04f + 0.08f * breath;
  uint32_t age = now - beatFlash;
  if (age < kBeatFlashMs) {
    float env = 1.f - static_cast<float>(age) / kBeatFlashMs;
    v += (0.35f + 0.55f * sFeedback) * env;
  }
  hw.led.SetHsv(ledHue, ledSat, Clamp(v, 0.f, 1.f));
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  static float curL = 24000.f;
  static float curR = 24000.f;
  bool fz = sFrozen;
  float fb = fz ? 1.f : sFeedback;
  float gain = sGain;
  float smoodge = sSmoodge;
  float dryAmp = sDryAmp;
  float wetAmp = sWetAmp;
  float tgtL = sTargetL;
  float tgtR = sTargetR;

  for (size_t i = 0; i < size; i++) {
    float dryL = in[0][i];
    float dryR = in[1][i];

    float chorusL = 0.f, chorusR = 0.f;
    if (!fz) {
      chorus.Process((dryL + dryR) * gain);
      chorusL = chorus.GetLeft();
      chorusR = chorus.GetRight();
    }

    fonepole(curL, tgtL, .0001f);
    fonepole(curR, tgtR, .0001f);
    delr.SetDelay(curL);
    dell.SetDelay(curR);
    float delayOutL = dell.Read();
    float delayOutR = delr.Read();

    float inL = fz ? 0.f : dryL + chorusL * smoodge;
    float inR = fz ? 0.f : dryR + chorusR * smoodge;
    dell.Write(fb * delayOutL + inL);
    delr.Write(fb * delayOutR + inR);

    delayOutL = tremL.Process(delayOutL);
    delayOutR = tremR.Process(delayOutR);

    float mixL = dryL * dryAmp + delayOutL * wetAmp;
    float mixR = dryR * dryAmp + delayOutR * wetAmp;
    out[0][i] = SoftLimit(mixL);
    out[1][i] = SoftLimit(mixR);
  }
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
  float sr = hw.SampleRate();

  dell.Init();
  delr.Init();
  chorus.Init(sr);
  tremL.Init(sr);
  tremR.Init(sr);
  tremL.SetWaveform(Oscillator::WAVE_SIN);
  tremR.SetWaveform(Oscillator::WAVE_SIN);
  tremL.SetFreq(3.9f);
  tremR.SetFreq(4.0f);
  flutter.Init(sr);
  flutter.SetWaveform(Oscillator::WAVE_SIN);
  flutter.SetAmp(0.f);
  flutter.SetFreq(0.05f);

  RunBootSweep();
  prevNow = System::GetNow();
  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();
    uint32_t now = System::GetNow();
    float dtMs = static_cast<float>(now - prevNow);
    prevNow = now;

    PollClock(now);
    PollButtons(now);
    UpdateControls();
    UpdateLed(now, dtMs);
    System::Delay(1);
  }
}
