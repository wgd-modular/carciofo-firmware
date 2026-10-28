#include <cmath>
#include <cstdint>
#include <cstring>

#include "../../lib/carciofo.h"
#include "../../lib/settings.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace carciofo;

/*
 * Coda. Clocked performance effects for the end of the chain.
 *
 * Every pot is one effect and is clean at noon. Turning it left or right
 * picks one of two flavours, getting stronger toward the end stop:
 *
 *   P1  low-pass   | high-pass, with a noise riser at the far end
 *   P2  reverb     | echo on 3/4 of a beat; the last stretch of either
 *                    side takes the dry signal away (wash-out, echo-out)
 *   P3  tape stop  | beat repeat, 1/4 down to 1/32 with triplets
 *   P4  pump       | gate, 1/8 down to 1/32
 *
 *   repeat/stop -> pump/gate -> echo + reverb sends -> filter -> soft clip
 *
 * CV1 is the clock. It takes 1, 4 or 24 pulses per beat and tells them apart
 * by their spacing. Without a clock an internal tempo runs, set by tapping B2.
 *
 * B1 switches all effects in or out on the next beat. Holding it makes the
 * switch momentary, a double tap is an immediate cut that drains the tails.
 * With the effects out the pots can be set up silently.
 *
 * CV2 throws the effects linked to it: each one moves from its pot position
 * toward the end stop on the side the pot is turned to, clockwise from noon.
 * Holding B2 and nudging a pot links or unlinks it.
 *
 * The audio callback reads the clock jack once per block, everything else
 * on the panel is read in the main loop.
 */

static const float kSampleRate = 48000.f;
static const int kNumPots = 4;

enum Effect { FX_FILTER = 0, FX_SPACE, FX_TIME, FX_AMP };

static const uint32_t kBufSize = 1u << 19;
static const uint32_t kBufMask = kBufSize - 1;
static const uint32_t kEchoSize = 1u << 18;
static const uint32_t kEchoMask = kEchoSize - 1;
static float DSY_SDRAM_BSS bufL[kBufSize];
static float DSY_SDRAM_BSS bufR[kBufSize];
static float DSY_SDRAM_BSS echoL[kEchoSize];
static float DSY_SDRAM_BSS echoR[kEchoSize];
static ReverbSc DSY_SDRAM_BSS reverb;

static Carciofo hw;

// Panel feel.
static const float kDeadIn = 0.10f;
static const float kDeadOut = 0.13f;
static const float kStepHyst = 0.18f;
static const float kNudge = 0.05f;
static const uint32_t kTapMs = 300;
static const uint32_t kHoldMs = 300;
static const uint32_t kDoubleTapMs = 300;

// Clock.
static const float kTrigRise = 0.20f;
static const float kTrigRearm = 0.07f;
static const float kRefractorySmp = 0.003f * kSampleRate;
static const float kGraceSmp = 0.040f * kSampleRate;
static const float kMinBeatSmp = kSampleRate * 60.f / 300.f;
static const float kMaxBeatSmp = kSampleRate * 60.f / 20.f;
static const float kWrapBeats = 64.f;

static const float kRollDiv[] = {1.f,         0.5f,        1.f / 3.f,
                                 0.25f,       1.f / 6.f,   0.125f};
static const int kNumRollSteps = 6;
static const float kGateDiv[] = {0.5f, 0.25f, 1.f / 6.f, 0.125f};
static const int kNumGateSteps = 4;

enum ClockMode { CLOCK_INTERNAL = 0, CLOCK_EXTERNAL, CLOCK_STOPPED };

// Main loop -> audio. Plain values, written in the main loop only.
static volatile int sSide[kNumPots] = {0, 0, 0, 0};
static volatile float sFilterHz = 1000.f;
static volatile float sFilterRes = 0.f;
static volatile float sNoise = 0.f;
static volatile float sDry = 1.f;
static volatile float sEchoFeed = 0.f;
static volatile float sEchoFb = 0.f;
static volatile float sEchoHpCoef = 0.02f;
static volatile float sSpaceFeed = 0.f;
static volatile float sSpaceFb = 0.85f;
static volatile int sRollStep = 0;
static volatile float sRollMix = 0.f;
static volatile float sStopBeats = 2.f;
static volatile int sGateStep = 0;
static volatile float sGateDepth = 0.f;
static volatile float sPumpDepth = 0.f;
static volatile float sTapPeriod = 24000.f;
static volatile uint32_t sToggleReq = 0;
static volatile uint32_t sPanicReq = 0;
static volatile uint32_t sPhaseReq = 0;
static volatile float sPhaseBeats = 0.f;
static volatile uint32_t sDownbeatReq = 0;
static volatile uint32_t sInternalReq = 0;

// Audio -> main loop, for the LED and the buttons.
static volatile int sClockMode = CLOCK_INTERNAL;
static volatile bool sFxOn = true;
static volatile bool sPending = false;
static volatile bool sHalted = false;
static volatile uint32_t sBeatSerial = 0;
static volatile int sBeatInBar = 0;
static volatile uint32_t sHealReq = 0;

// Audio state.
static uint32_t tNow = 0;
static float pos = 0.f;
static float posLimit = 1.f;
static float beatPeriod = 24000.f;
static int clockMode = CLOCK_INTERNAL;
static bool fxOn = true;
static bool pending = false;
static int lastBeat = 0;
static int barOffset = 0;
static uint32_t toggleSeen = 0, panicSeen = 0, phaseSeen = 0, downbeatSeen = 0,
                internalSeen = 0;

static float clkBaseline = 0.f;
static bool clkArmed = false;
static float clkRefractory = 0.f;
static float clkSettle = 0.2f * kSampleRate;
static uint32_t lastPulse = 0;
static float pulseIv[3] = {0.f, 0.f, 0.f};
static int pulseIvCount = 0;
static int pulseIvIdx = 0;
static float pulsePeriod = 24000.f;
static int ppqn = 1;
static int pulseIdx = 0;
static uint32_t pulseTimes[32];
static int pulseCount = 0;
static bool ppqnFresh = true;

static float fxGain = 1.f;
static float drainSmp = 0.f;

enum RollState { ROLL_IDLE = 0, ROLL_ARMED, ROLL_ON };
static int rollState = ROLL_IDLE;
static int rollArmCell = 0;
static float rollArmDiv = 1.f;
static uint32_t rollStart = 0;
static float rollPh = 0.f;
static float rollLen = 24000.f;
static float rollElapsed = 0.f;
static float rollTailPh = 0.f;
static float rollXfade = 1.f;
static float rollFade = 0.f;
static float rollMix = 0.f;

enum StopState { STOP_IDLE = 0, STOP_ARMED, STOP_BRAKING, STOP_RETURN };
static int stopState = STOP_IDLE;
static float stopSpeed = 1.f;
static float stopDecel = 0.f;
static float stopLag = 0.f;
static float stopFade = 0.f;

static float ampGain = 1.f;
static float dryGain = 1.f;
static float echoFeed = 0.f;
static float echoFb = 0.f;
static float echoTime = 18000.f;
static uint32_t echoW = 0;
static float echoHpL = 0.f, echoHpR = 0.f, echoLpL = 0.f, echoLpR = 0.f;
static float tailGain = 1.f;
static float spaceFeed = 0.f;
static float spaceHpL = 0.f, spaceHpR = 0.f;

static float filterHz = 1000.f;
static float mixLp = 0.f, mixHp = 0.f;
static float svfIc1[2] = {0.f, 0.f}, svfIc2[2] = {0.f, 0.f};
static float svfA1 = 0.f, svfA2 = 0.f, svfA3 = 0.f, svfK = 2.f;
static float noiseLevel = 0.f;
static uint32_t noiseSeed = 0x9e3779b9u;

static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static inline float Saturate(float x) {
  if (x > 3.f) return 1.f;
  if (x < -3.f) return -1.f;
  float x2 = x * x;
  return x * (27.f + x2) / (27.f + 9.f * x2);
}

// Clean below -1 dBFS, bends gently above.
static inline float Limit(float x) {
  const float knee = 0.89f;
  const float room = 1.f - knee;
  if (x > knee) return knee + room * Saturate((x - knee) / room);
  if (x < -knee) return -knee + room * Saturate((x + knee) / room);
  return x;
}

static inline float Noise() {
  noiseSeed ^= noiseSeed << 13;
  noiseSeed ^= noiseSeed >> 17;
  noiseSeed ^= noiseSeed << 5;
  return static_cast<int32_t>(noiseSeed) * 4.6566129e-10f;
}

static inline float ReadAt(const float* buf, uint32_t mask, uint32_t base,
                           float offset) {
  float fl = floorf(offset);
  float f = offset - fl;
  uint32_t a = (base + static_cast<uint32_t>(static_cast<int32_t>(fl))) & mask;
  return buf[a] + f * (buf[(a + 1) & mask] - buf[a]);
}

static inline float Frac(float x) { return x - floorf(x); }

/* Grid lines are tracked by cell numbers. After a jump in the position the
   trackers are moved along without firing. */
static void Resync() {
  lastBeat = static_cast<int>(floorf(pos));
  if (rollState == ROLL_ARMED)
    rollArmCell = static_cast<int>(floorf(pos / rollArmDiv));
}

static void SetPosition(float p) {
  pos = p;
  while (pos >= kWrapBeats) pos -= kWrapBeats;
  while (pos < 0.f) pos += kWrapBeats;
  Resync();
}

static void Toggle() {
  fxOn = !fxOn;
  pending = false;
}

static float PastBeatLine(float div) {
  return Frac(pos / div) * div * beatPeriod;
}

static void OnPulse() {
  bool restart = clockMode != CLOCK_EXTERNAL;
  float iv = static_cast<float>(tNow - lastPulse);
  lastPulse = tNow;
  if (!restart && iv > 0.f) {
    pulseIv[pulseIvIdx] = iv;
    pulseIvIdx = (pulseIvIdx + 1) % 3;
    if (pulseIvCount < 3) pulseIvCount++;
    float a = pulseIv[0], b = pulseIv[1], c = pulseIv[2];
    float med = pulseIvCount < 3
                    ? pulseIv[(pulseIvIdx + 2) % 3]
                    : fmaxf(fminf(a, b), fminf(fmaxf(a, b), c));
    pulsePeriod = med;

    float ms = med * 1000.f / kSampleRate;
    // A fresh clock is judged on its own, a running one only switches when
    // it is clearly outside its range.
    int next = ppqn;
    if (ppqnFresh) next = ms >= 255.f ? 1 : (ms >= 55.f ? 4 : 24);
    ppqnFresh = false;
    if (ppqn == 1 && ms < 243.f) next = ms < 57.f ? 24 : 4;
    if (ppqn == 4 && (ms > 297.f || ms < 47.f)) next = ms > 297.f ? 1 : 24;
    if (ppqn == 24 && ms > 63.f) next = ms > 270.f ? 1 : 4;
    if (next != ppqn) {
      int beat = pulseIdx / ppqn;
      ppqn = next;
      pulseIdx = beat * ppqn;
    }
    // Edges are only seen once per block, so with many pulses per beat the
    // tempo is taken over a whole beat of them.
    float beat = pulsePeriod * ppqn;
    if (ppqn > 1 && pulseCount >= ppqn)
      beat = static_cast<float>(tNow - pulseTimes[(pulseCount - ppqn) % 32]);
    beatPeriod = Clamp(beat, kMinBeatSmp, kMaxBeatSmp);
  }
  if (restart) pulseCount = 0;
  pulseTimes[pulseCount % 32] = tNow;
  pulseCount++;

  int wrap = static_cast<int>(kWrapBeats) * ppqn;
  if (restart) {
    clockMode = CLOCK_EXTERNAL;
    ppqnFresh = true;
    pulsePeriod = kMaxBeatSmp;
    pulseIvCount = 0;
    pulseIdx = 0;
    barOffset = 0;
    SetPosition(0.f);
    lastBeat = -1;
  } else {
    pulseIdx = (pulseIdx + 1) % wrap;
    float p = static_cast<float>(pulseIdx) / ppqn;
    if (p < pos && pos - p < kWrapBeats * 0.5f) {
      SetPosition(p);  // a ppqn change can step back, never fire twice
    } else {
      pos = p;
    }
  }
  posLimit = static_cast<float>(pulseIdx + 1) / ppqn - 1e-4f;
}

static void PollClock(float cv, size_t size) {
  float dt = static_cast<float>(size);
  if (clkRefractory > 0.f) clkRefractory -= dt;
  if (clkSettle > 0.f) {
    clkSettle -= dt;
    clkBaseline += 0.06f * (cv - clkBaseline);
    return;
  }
  if (!clkArmed) {
    clkBaseline += 0.006f * (cv - clkBaseline);
    if (clkRefractory <= 0.f && cv > clkBaseline + kTrigRise) {
      clkArmed = true;
      clkRefractory = kRefractorySmp;
      OnPulse();
    }
  } else if (cv < clkBaseline + kTrigRearm) {
    clkArmed = false;
  }

  if (clockMode == CLOCK_EXTERNAL &&
      static_cast<float>(tNow - lastPulse) > 2.5f * pulsePeriod + 480.f) {
    clockMode = CLOCK_STOPPED;
    if (pending) Toggle();
  }
}

static void HandleRequests() {
  if (internalSeen != sInternalReq) {
    internalSeen = sInternalReq;
    if (clockMode != CLOCK_EXTERNAL) clockMode = CLOCK_INTERNAL;
  }
  if (clockMode == CLOCK_INTERNAL) beatPeriod = sTapPeriod;

  if (phaseSeen != sPhaseReq) {
    phaseSeen = sPhaseReq;
    if (clockMode != CLOCK_EXTERNAL) {
      barOffset = 0;
      SetPosition(sPhaseBeats);
    }
  }
  if (downbeatSeen != sDownbeatReq) {
    downbeatSeen = sDownbeatReq;
    int nearest = static_cast<int>(floorf(pos + 0.5f));
    barOffset = nearest;
  }
  if (toggleSeen != sToggleReq) {
    toggleSeen = sToggleReq;
    if (clockMode == CLOCK_EXTERNAL) {
      if (!pending && PastBeatLine(1.f) < kGraceSmp) {
        Toggle();
      } else {
        pending = !pending;
      }
    } else {
      Toggle();
      if (fxOn && clockMode == CLOCK_INTERNAL) {
        barOffset = 0;
        SetPosition(0.f);
      }
    }
  }
  if (panicSeen != sPanicReq) {
    panicSeen = sPanicReq;
    fxOn = false;
    pending = false;
    drainSmp = fmaxf(0.4f * kSampleRate, echoTime + 0.05f * kSampleRate);
    rollState = ROLL_IDLE;
    rollFade = 0.f;
    stopState = STOP_IDLE;
    stopFade = 0.f;
  }
}

static void OnBeat() {
  if (pending) Toggle();
  if (stopState == STOP_ARMED) {
    stopState = STOP_BRAKING;
    stopSpeed = 1.f;
    stopLag = 0.f;
  } else if (stopState == STOP_RETURN) {
    stopState = STOP_IDLE;
  }
  sBeatInBar = ((lastBeat - barOffset) % 4 + 4) % 4;
  sBeatSerial = sBeatSerial + 1;
}

// The slice plays sample by sample; the grid only decides where each repeat
// starts, so a clock edge seen a block late never stalls the audio.
static void StartRoll(float pastSmp) {
  rollState = ROLL_ON;
  rollStart = tNow - static_cast<uint32_t>(pastSmp);
  rollPh = floorf(pastSmp);
  rollElapsed = pastSmp / beatPeriod;
  rollLen = kRollDiv[sRollStep] * beatPeriod;
  rollXfade = 1.f;
}

static void UpdateRoll(float dpos) {
  bool want = fxOn && sSide[FX_TIME] > 0 && clockMode != CLOCK_STOPPED &&
              drainSmp <= 0.f;
  float div = kRollDiv[sRollStep];
  switch (rollState) {
    case ROLL_IDLE:
      if (!want) break;
      if (clockMode == CLOCK_EXTERNAL) {
        float past = PastBeatLine(div);
        if (past < kGraceSmp) {
          StartRoll(past);
        } else {
          rollState = ROLL_ARMED;
          rollArmDiv = div;
          rollArmCell = static_cast<int>(floorf(pos / div));
        }
      } else {
        StartRoll(0.f);
      }
      break;
    case ROLL_ARMED:
      if (!want) {
        rollState = ROLL_IDLE;
      } else if (static_cast<int>(floorf(pos / rollArmDiv)) != rollArmCell) {
        StartRoll(0.f);
      }
      break;
    case ROLL_ON: {
      if (!want) {
        rollState = ROLL_IDLE;
        break;
      }
      float before = rollElapsed;
      rollElapsed += dpos;
      rollPh += 1.f;
      rollTailPh += 1.f;
      if (floorf(rollElapsed / div) != floorf(before / div)) {
        rollTailPh = rollPh;
        rollPh = 0.f;
        rollLen = div * beatPeriod;
        rollXfade = 0.f;
      } else if (rollPh >= 1.1f * rollLen + 256.f) {
        rollTailPh = rollPh;
        rollPh = 0.f;
        rollXfade = 0.f;
      }
      return;
    }
  }
  // Still fading out: keep the slice moving so it does not buzz.
  rollPh += 1.f;
  rollTailPh += 1.f;
  if (rollPh >= rollLen) rollPh = 0.f;
}

static void StartStop() {
  stopState = STOP_BRAKING;
  stopSpeed = 1.f;
  stopLag = 0.f;
}

static void UpdateStop() {
  bool want = fxOn && sSide[FX_TIME] < 0 && clockMode != CLOCK_STOPPED &&
              drainSmp <= 0.f;
  bool quantise = clockMode == CLOCK_EXTERNAL;
  switch (stopState) {
    case STOP_IDLE:
      if (!want) break;
      if (quantise && PastBeatLine(1.f) >= kGraceSmp) {
        stopState = STOP_ARMED;
      } else {
        StartStop();
      }
      break;
    case STOP_ARMED:
      if (!want) stopState = STOP_IDLE;
      break;
    case STOP_BRAKING:
      if (!want) stopState = quantise && fxOn ? STOP_RETURN : STOP_IDLE;
      break;
    case STOP_RETURN:
      if (want) {
        stopState = STOP_BRAKING;
      } else if (!quantise || !fxOn) {
        stopState = STOP_IDLE;
      }
      break;
  }
  if (stopState == STOP_BRAKING || stopState == STOP_RETURN) {
    stopSpeed -= stopDecel;
    if (stopSpeed < 0.f) stopSpeed = 0.f;
    stopLag += 1.f - stopSpeed;
  }
}

static void UpdateBlock() {
  float hz = sFilterHz;
  filterHz += 0.2f * (hz - filterHz);
  float g = tanf(3.14159265f * Clamp(filterHz, 15.f, 20000.f) / kSampleRate);
  svfK = 2.f - 2.f * sFilterRes;
  svfA1 = 1.f / (1.f + g * (g + svfK));
  svfA2 = g * svfA1;
  svfA3 = g * svfA2;

  stopDecel = 1.f / (sStopBeats * beatPeriod);
  float maxEcho = static_cast<float>(kEchoSize - 8);
  float wantEcho = fminf(0.75f * beatPeriod, maxEcho);
  echoTime += 0.05f * (wantEcho - echoTime);

  float fb = drainSmp > 0.f ? 0.f : sSpaceFb;
  reverb.SetFeedback(fb);
}

static void AudioCallback(AudioHandle::InputBuffer in,
                          AudioHandle::OutputBuffer out, size_t size) {
  PollClock(hw.GetCv(CV_1), size);
  HandleRequests();
  UpdateBlock();

  const float smooth = 0.004f;
  const float ampSmooth = 0.012f;
  float check = 0.f;
  int filterSide = sSide[FX_FILTER];
  int ampSide = sSide[FX_AMP];
  float echoHpCoef = sEchoHpCoef;
  bool gridOn = clockMode != CLOCK_STOPPED;

  for (size_t i = 0; i < size; i++) {
    float prevPos = pos;
    if (clockMode == CLOCK_EXTERNAL) {
      pos += 1.f / beatPeriod;
      if (pos > posLimit) pos = posLimit;
    } else if (clockMode == CLOCK_INTERNAL) {
      pos += 1.f / beatPeriod;
      if (pos >= kWrapBeats) pos -= kWrapBeats;
    }
    float dpos = pos - prevPos;
    if (dpos < -kWrapBeats * 0.5f) dpos += kWrapBeats;
    if (dpos < 0.f) dpos = 0.f;
    int beat = static_cast<int>(floorf(pos));
    if (beat != lastBeat && gridOn) {
      lastBeat = beat;
      OnBeat();
    }

    float inL = in[0][i];
    float inR = in[1][i];
    // A held roll keeps its slice from being written over.
    bool sliceFull = rollState == ROLL_ON &&
                     static_cast<float>(tNow - rollStart) >
                         1.25f * beatPeriod + 512.f;
    if (!sliceFull) {
      uint32_t w = tNow & kBufMask;
      bufL[w] = inL;
      bufR[w] = inR;
    }

    fxGain += ((fxOn ? 1.f : 0.f) - fxGain) * smooth;
    if (drainSmp > 0.f) drainSmp -= 1.f;

    // Repeat and tape stop.
    UpdateRoll(dpos);
    float rollTarget = rollState == ROLL_ON ? 1.f : 0.f;
    rollFade += (rollTarget - rollFade) * 0.02f;
    rollMix += (sRollMix - rollMix) * smooth;
    float xL = inL, xR = inR;
    if (rollFade > 1e-4f) {
      float off = rollPh;
      uint32_t base = rollStart;
      float rL = ReadAt(bufL, kBufMask, base, off);
      float rR = ReadAt(bufR, kBufMask, base, off);
      if (rollXfade < 1.f) {
        float toff = rollTailPh;
        float tL = ReadAt(bufL, kBufMask, base, toff);
        float tR = ReadAt(bufR, kBufMask, base, toff);
        rL = Lerp(tL, rL, rollXfade);
        rR = Lerp(tR, rR, rollXfade);
        rollXfade += 1.f / 72.f;
      }
      float m = rollFade * rollMix;
      xL = Lerp(xL, rL, m);
      xR = Lerp(xR, rR, m);
    }

    UpdateStop();
    float stopTarget =
        stopState == STOP_BRAKING || stopState == STOP_RETURN ? 1.f : 0.f;
    stopFade += (stopTarget - stopFade) * 0.02f;
    if (stopFade > 1e-4f) {
      float sL = ReadAt(bufL, kBufMask, tNow, -stopLag);
      float sR = ReadAt(bufR, kBufMask, tNow, -stopLag);
      float level = fminf(1.f, stopSpeed * 4.f);
      xL = Lerp(xL, sL * level, stopFade);
      xR = Lerp(xR, sR * level, stopFade);
    } else if (stopState == STOP_IDLE) {
      stopLag = 0.f;
      stopSpeed = 1.f;
    }

    // Pump and gate.
    float ampTarget = 1.f;
    if (gridOn && ampSide > 0) {
      float ph = Frac(pos / kGateDiv[sGateStep]);
      ampTarget = ph < 0.5f ? 1.f : 1.f - sGateDepth;
    } else if (gridOn && ampSide < 0) {
      float ph = Frac(pos);
      float env = ph < 0.015f ? ph / 0.015f : 1.f - (ph - 0.015f) / 0.55f;
      env = env > 0.f ? env * env : 0.f;
      ampTarget = 1.f - sPumpDepth * env;
    }
    ampTarget = 1.f + fxGain * (ampTarget - 1.f);
    ampGain += (ampTarget - ampGain) * ampSmooth;
    xL *= ampGain;
    xR *= ampGain;

    // Echo and reverb, fed as sends so their tails ring on.
    dryGain += ((1.f + fxGain * (sDry - 1.f)) - dryGain) * smooth;
    echoFeed += (fxGain * sEchoFeed - echoFeed) * smooth;
    spaceFeed += (fxGain * sSpaceFeed - spaceFeed) * smooth;
    float fbTarget = drainSmp > 0.f ? 0.f : sEchoFb;
    if (!fxOn && fbTarget > 0.85f) fbTarget = 0.85f;
    echoFb += (fbTarget - echoFb) * smooth;
    tailGain += ((drainSmp > 0.f ? 0.f : 1.f) - tailGain) * 0.01f;

    float eL = ReadAt(echoL, kEchoMask, echoW, -echoTime);
    float eR = ReadAt(echoR, kEchoMask, echoW, -echoTime);
    echoHpL += echoHpCoef * (eL - echoHpL);
    echoHpR += echoHpCoef * (eR - echoHpR);
    float fL = eL - echoHpL, fR = eR - echoHpR;
    echoLpL += 0.6f * (fL - echoLpL);
    echoLpR += 0.6f * (fR - echoLpR);
    echoL[echoW & kEchoMask] = Saturate(xL * echoFeed + echoLpL * echoFb);
    echoR[echoW & kEchoMask] = Saturate(xR * echoFeed + echoLpR * echoFb);
    echoW++;

    spaceHpL += 0.02f * (xL - spaceHpL);
    spaceHpR += 0.02f * (xR - spaceHpR);
    float vL = 0.f, vR = 0.f;
    reverb.Process((xL - spaceHpL) * spaceFeed, (xR - spaceHpR) * spaceFeed,
                   &vL, &vR);

    float yL = xL * dryGain + (echoLpL + 0.9f * vL) * tailGain;
    float yR = xR * dryGain + (echoLpR + 0.9f * vR) * tailGain;

    // DJ filter over everything, tails included.
    float lpTarget = filterSide < 0 ? fxGain : 0.f;
    float hpTarget = filterSide > 0 ? fxGain : 0.f;
    mixLp += (lpTarget - mixLp) * smooth;
    mixHp += (hpTarget - mixHp) * smooth;
    noiseLevel += ((filterSide > 0 ? sNoise * fxGain : 0.f) - noiseLevel) *
                  smooth;
    if (mixLp > 1e-4f || mixHp > 1e-4f) {
      float nz = noiseLevel > 1e-5f ? noiseLevel : 0.f;
      float y[2] = {yL + nz * Noise(), yR + nz * Noise()};
      float dry[2] = {yL, yR};
      for (int c = 0; c < 2; c++) {
        float v3 = y[c] - svfIc2[c];
        float v1 = svfA1 * svfIc1[c] + svfA2 * v3;
        float v2 = svfIc2[c] + svfA2 * svfIc1[c] + svfA3 * v3;
        svfIc1[c] = 2.f * v1 - svfIc1[c];
        svfIc2[c] = 2.f * v2 - svfIc2[c];
        float hp = y[c] - svfK * v1 - v2;
        y[c] = dry[c] + mixLp * (v2 - dry[c]) + mixHp * (hp - dry[c]);
      }
      yL = y[0];
      yR = y[1];
    } else {
      svfIc1[0] = svfIc1[1] = svfIc2[0] = svfIc2[1] = 0.f;
    }

    out[0][i] = Limit(yL);
    out[1][i] = Limit(yR);
    check += yL + yR;
    tNow++;
  }

  if (!std::isfinite(check)) {
    svfIc1[0] = svfIc1[1] = svfIc2[0] = svfIc2[1] = 0.f;
    echoHpL = echoHpR = echoLpL = echoLpR = 0.f;
    spaceHpL = spaceHpR = 0.f;
    filterHz = 1000.f;
    sHealReq = sHealReq + 1;
  }

  sClockMode = clockMode;
  sFxOn = fxOn;
  sPending = pending;
  sHalted = stopFade > 0.5f && stopSpeed <= 0.f;
}

// ---------------------------------------------------------------- controls

static const float kPotRgb[kNumPots][3] = {
    {0.05f, 0.30f, 1.00f},  // filter, blue
    {0.10f, 1.00f, 0.25f},  // space and echo, green
    {1.00f, 0.06f, 0.02f},  // stop and repeat, red
    {1.00f, 0.50f, 0.00f},  // pump and gate, amber
};

struct Saved {
  int32_t link[kNumPots];
  float tapMs;
  bool operator==(const Saved& o) const {
    for (int i = 0; i < kNumPots; i++)
      if (link[i] != o.link[i]) return false;
    return fabsf(tapMs - o.tapMs) < 0.5f;
  }
};
static Settings<Saved> settings;

static float potSmooth[kNumPots];
static float potHeld[kNumPots];
static bool knobInZone[kNumPots] = {true, true, true, true};
static bool effInZone[kNumPots] = {true, true, true, true};
static int effSide[kNumPots] = {0, 0, 0, 0};
static float effAmount[kNumPots] = {0.f, 0.f, 0.f, 0.f};
static bool linked[kNumPots] = {true, false, false, false};
static float cv2 = 0.f;
static int rollStep = 0;
static int gateStep = 0;

static uint32_t lastMoveTime = 0;
static int lastMoved = -1;
static uint32_t zoneBlipTime = 0;
static uint32_t linkFlashTime = 0;
static int linkFlashPot = 0;
static uint32_t tapFlashTime = 0;

static uint32_t b1PressTime = 0;
static uint32_t b1ReleaseTime = 0;
static bool b1Momentary = false;
static bool b1Panic = false;
static bool b1LastWasTap = false;

static uint32_t b2PressTime = 0;
static bool b2Nudged = false;
static bool b2Toggled[kNumPots];
static float b2Base[kNumPots];

static float tapMs = 500.f;
static uint32_t lastTap = 0;
static float tapIv[3];
static int tapCount = 0;
static int tapBeat = 0;

static uint32_t lastBeatSerial = 0;
static uint32_t beatFlashTime = 0;
static int beatFlashPot = 0;

static float Expo(float t, float lo, float hi) {
  return lo * powf(hi / lo, t);
}

static int StepWithHysteresis(float x, int cur, int n) {
  if (x > cur + 1.f + kStepHyst || x < cur - kStepHyst)
    cur = static_cast<int>(floorf(x));
  if (cur < 0) cur = 0;
  if (cur > n - 1) cur = n - 1;
  return cur;
}

static void RegisterTap(uint32_t at) {
  uint32_t iv = at - lastTap;
  if (lastTap != 0 && iv >= 150 && iv <= 2000) {
    tapIv[tapCount % 3] = static_cast<float>(iv);
    tapCount++;
    int n = tapCount < 3 ? tapCount : 3;
    float sum = 0.f;
    for (int i = 0; i < n; i++) sum += tapIv[i];
    tapMs = sum / n;
    tapBeat = (tapBeat + 1) % 4;
  } else {
    tapCount = 0;
    tapBeat = 0;
  }
  lastTap = at;
  sTapPeriod = tapMs * kSampleRate / 1000.f;
  float since = static_cast<float>(System::GetNow() - at) / tapMs;
  sPhaseBeats = tapBeat + since;
  sPhaseReq = sPhaseReq + 1;
  sInternalReq = sInternalReq + 1;
}

static void ReadButtons(uint32_t now) {
  Switch& b1 = hw.button[BUTTON_1];
  Switch& b2 = hw.button[BUTTON_2];

  if (b1.RisingEdge()) {
    b1PressTime = now;
    b1Momentary = false;
    b1Panic = b1LastWasTap && now - b1ReleaseTime < kDoubleTapMs;
    if (b1Panic) {
      sPanicReq = sPanicReq + 1;
    } else {
      sToggleReq = sToggleReq + 1;
    }
  }
  if (b1.Pressed() && !b1Panic && now - b1PressTime >= kHoldMs)
    b1Momentary = true;
  if (b1.FallingEdge()) {
    if (b1Momentary) sToggleReq = sToggleReq + 1;
    b1LastWasTap = !b1Momentary && !b1Panic;
    b1ReleaseTime = now;
  }

  if (b2.RisingEdge()) {
    b2PressTime = now;
    b2Nudged = false;
    for (int i = 0; i < kNumPots; i++) {
      b2Toggled[i] = false;
      b2Base[i] = potSmooth[i];
    }
  }
  if (b2.Pressed()) {
    for (int i = 0; i < kNumPots; i++) {
      if (b2Toggled[i] || fabsf(potSmooth[i] - b2Base[i]) < kNudge) continue;
      b2Toggled[i] = true;
      b2Nudged = true;
      linked[i] = !linked[i];
      linkFlashTime = now;
      linkFlashPot = i;
    }
  }
  if (b2.FallingEdge() && !b2Nudged && now - b2PressTime < kTapMs) {
    tapFlashTime = now;
    if (sClockMode == CLOCK_EXTERNAL) {
      sDownbeatReq = sDownbeatReq + 1;
    } else {
      RegisterTap(b2PressTime);
    }
  }
}

/* Turns a bipolar position into a side and an amount, with a dead zone at
   noon that holds on to its state across small wobbles. */
static void Zone(float e, bool& inZone, int& side, float& amount) {
  float m = fabsf(e);
  if (inZone && m > kDeadOut) inZone = false;
  if (!inZone && m < kDeadIn) inZone = true;
  side = inZone ? 0 : (e > 0.f ? 1 : -1);
  amount = inZone ? 0.f : Clamp((m - kDeadIn) / (1.f - kDeadIn), 0.f, 1.f);
}

static void ReadKnobs(uint32_t now) {
  bool shift = hw.button[BUTTON_2].Pressed();
  for (int i = 0; i < kNumPots; i++) {
    float raw = hw.GetPot(static_cast<Pot>(i));
    float before = potSmooth[i];
    potSmooth[i] += 0.1f * (raw - potSmooth[i]);

    // While B2 is held the pots only link, the effects keep their values.
    if (!shift) {
      if (fabsf(potSmooth[i] - potHeld[i]) > 0.004f) potHeld[i] = potSmooth[i];
      if (fabsf(potSmooth[i] - before) > 0.0015f) {
        lastMoveTime = now;
        lastMoved = i;
      }
    }
    float b = 2.f * potHeld[i] - 1.f;

    bool wasIn = knobInZone[i];
    int knobSide;
    float knobAmount;
    Zone(b, knobInZone[i], knobSide, knobAmount);
    if (knobInZone[i] && !wasIn) zoneBlipTime = now;

    float e = b;
    if (linked[i]) {
      float dir = knobSide != 0 ? static_cast<float>(knobSide) : 1.f;
      e = b + cv2 * (dir - b);
    }
    Zone(e, effInZone[i], effSide[i], effAmount[i]);
  }
}

static void ApplyEffects() {
  int fs = effSide[FX_FILTER];
  float fa = effAmount[FX_FILTER];
  if (fs < 0) {
    sFilterHz = Expo(fa, 18000.f, 90.f);
    sFilterRes = 0.12f + 0.48f * fa;
  } else if (fs > 0) {
    float hz = Expo(fa, 25.f, 7000.f);
    sFilterHz = hz;
    float res = 0.12f + 0.48f * fa;
    if (hz < 250.f) res *= hz / 250.f;
    sFilterRes = res;
  }
  float riser = Clamp((fa - 0.6f) / 0.4f, 0.f, 1.f);
  sNoise = fs > 0 ? 0.12f * riser * riser : 0.f;

  int ss = effSide[FX_SPACE];
  float sa = effAmount[FX_SPACE];
  float send = Clamp(sa / 0.3f, 0.f, 1.f);
  float out = Clamp((sa - 0.85f) / 0.15f, 0.f, 1.f);
  if (ss > 0) {
    sEchoFeed = 0.8f * send * (1.f - out);
    sEchoFb = 0.35f + 0.50f * sa + 0.07f * out;
    float hpHz = Expo(sa, 150.f, 500.f);
    sEchoHpCoef = 1.f - expf(-6.2831853f * hpHz / kSampleRate);
    sSpaceFeed = 0.f;
    sDry = 1.f - out;
  } else if (ss < 0) {
    sSpaceFeed = send;
    sSpaceFb = 0.80f + 0.14f * sa;
    sEchoFeed = 0.f;
    sDry = (1.f - 0.2f * send) * (1.f - out);
  } else {
    sEchoFeed = 0.f;
    sSpaceFeed = 0.f;
    sDry = 1.f;
  }

  int ts = effSide[FX_TIME];
  float ta = effAmount[FX_TIME];
  if (ts > 0) {
    float x = ta < 0.2f ? ta / 0.2f * 0.999f : 1.f + (ta - 0.2f) / 0.8f * 5.f;
    rollStep = StepWithHysteresis(x, rollStep, kNumRollSteps);
    sRollStep = rollStep;
    sRollMix = Clamp(ta / 0.2f, 0.f, 1.f);
  } else if (ts < 0) {
    sStopBeats = Expo(ta, 4.f, 0.5f);
  }
  sSide[FX_TIME] = ts;

  int as = effSide[FX_AMP];
  float aa = effAmount[FX_AMP];
  if (as > 0) {
    float x =
        aa < 0.25f ? aa / 0.25f * 0.999f : 1.f + (aa - 0.25f) / 0.75f * 3.f;
    gateStep = StepWithHysteresis(x, gateStep, kNumGateSteps);
    sGateStep = gateStep;
    sGateDepth = Clamp(aa / 0.25f, 0.f, 1.f);
  } else if (as < 0) {
    sPumpDepth = 0.85f * aa;
  }
  sSide[FX_AMP] = as;
  sSide[FX_FILTER] = fs;
  sSide[FX_SPACE] = ss;
}

// --------------------------------------------------------------------- LED

static void SetRgb(const float* rgb, float v, float white = 0.f) {
  hw.led.Set(rgb[0] * v * (1.f - white) + v * white,
             rgb[1] * v * (1.f - white) + v * white,
             rgb[2] * v * (1.f - white) + v * white);
}

static void UpdateLed(uint32_t now) {
  if (sBeatSerial != lastBeatSerial) {
    lastBeatSerial = sBeatSerial;
    beatFlashTime = now;
    beatFlashPot = sBeatInBar;
  }

  if (now - tapFlashTime < 70) {
    hw.led.Set(0.8f, 0.8f, 0.8f);
    return;
  }

  // Holding B2 reads out the links, a nudge confirms its own.
  if (hw.button[BUTTON_2].Pressed()) {
    if (now - linkFlashTime < 250) {
      if (linked[linkFlashPot]) {
        SetRgb(kPotRgb[linkFlashPot], 1.f);
      } else {
        hw.led.Set(0.04f, 0.04f, 0.04f);
      }
      return;
    }
    int list[kNumPots], n = 0;
    for (int i = 0; i < kNumPots; i++)
      if (linked[i]) list[n++] = i;
    if (n == 0) {
      hw.led.Set(0.06f, 0.06f, 0.06f);
    } else {
      SetRgb(kPotRgb[list[(now / 350) % n]], 0.5f);
    }
    return;
  }

  if (now - zoneBlipTime < 60) {
    hw.led.Set(1.f, 1.f, 1.f);
    return;
  }

  bool fx = sFxOn;
  if (sPending) {
    bool on = (now % 100) < 50;
    float v = fx ? 0.25f : 0.9f;
    hw.led.Set(on ? v : 0.f, on ? v : 0.f, on ? v : 0.f);
    return;
  }

  if (lastMoved >= 0 && now - lastMoveTime < 700) {
    int i = lastMoved;
    float v = effSide[i] == 0 ? 0.04f : 0.15f + 0.85f * effAmount[i];
    if (!fx) v *= 0.3f;
    SetRgb(kPotRgb[i], v, effSide[i] < 0 ? 0.35f : 0.f);
    return;
  }

  if (sHalted && fx) {
    SetRgb(kPotRgb[FX_TIME], 0.5f);
    return;
  }

  if (sClockMode == CLOCK_STOPPED) {
    float breath = 0.5f + 0.5f * sinf(6.2831853f * (now % 2400) / 2400.f);
    float v = 0.01f + 0.06f * breath;
    hw.led.Set(v, v, v);
    return;
  }

  // One beat per pot: the bar reads out the whole set.
  uint32_t since = now - beatFlashTime;
  if (since < 90) {
    int i = beatFlashPot;
    float scale = fx ? 1.f : 0.2f;
    if (effSide[i] != 0) {
      SetRgb(kPotRgb[i], scale * (0.25f + 0.75f * effAmount[i]),
             effSide[i] < 0 ? 0.35f : 0.f);
    } else {
      float v = scale * (i == 0 ? 0.22f : 0.08f);
      hw.led.Set(v, v, v);
    }
    return;
  }
  hw.led.Set(0.f, 0.f, 0.f);
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

static uint32_t healSeen = 0;

static void Setup() {
  memset(bufL, 0, sizeof(bufL));
  memset(bufR, 0, sizeof(bufR));
  memset(echoL, 0, sizeof(echoL));
  memset(echoR, 0, sizeof(echoR));
  reverb.Init(kSampleRate);
  reverb.SetLpFreq(9000.f);
  reverb.SetFeedback(0.85f);

  settings.Init(hw, SLOT_CODA, 1, Saved{{1, 0, 0, 0}, 500.f});
  const Saved& saved = settings.Get();
  for (int i = 0; i < kNumPots; i++) linked[i] = saved.link[i] != 0;
  tapMs = Clamp(saved.tapMs, 150.f, 2000.f);
  sTapPeriod = tapMs * kSampleRate / 1000.f;
  beatPeriod = sTapPeriod;

  for (int i = 0; i < kNumPots; i++) {
    potSmooth[i] = potHeld[i] = hw.GetPot(static_cast<Pot>(i));
  }
}

static void ControlPass() {
  hw.ProcessControls();
  uint32_t now = System::GetNow();

  cv2 += 0.5f * (hw.GetCv(CV_2) - cv2);
  ReadButtons(now);
  ReadKnobs(now);
  ApplyEffects();
  UpdateLed(now);

  if (healSeen != sHealReq) {
    healSeen = sHealReq;
    memset(echoL, 0, sizeof(echoL));
    memset(echoR, 0, sizeof(echoR));
  }

  Saved s;
  for (int i = 0; i < kNumPots; i++) s.link[i] = linked[i] ? 1 : 0;
  s.tapMs = tapMs;
  settings.Get() = s;
  settings.Process();
}

int main(void) {
  hw.Init();
  Setup();
  RunBootSweep();
  hw.StartAudio(AudioCallback);
  while (1) {
    ControlPass();
    System::Delay(1);
  }
}
