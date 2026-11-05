#include <algorithm>
#include <cmath>
#include <cstring>

#include "../../lib/carciofo.h"
#include "../../lib/settings.h"
#include "beads/dsp/processor.h"
#include "beads/gain_control.h"
#include "stmlib/dsp/dsp.h"
#include "stmlib/utils/gate_flags.h"
#include "stmlib/utils/random.h"
#include "tables.h"

using namespace carciofo;
using namespace beads;
using daisy::System;
using stmlib::GateFlags;

/*
 * Grani. Beads' granular processor behind four pots, two CV inputs, two
 * buttons and one LED.
 *
 *            main       hold B2    hold B1
 *   P1       time       feedback   time random
 *   P2       size       dry/wet    size random
 *   P3       pitch      reverb     pitch random
 *   P4       density    shape      shape random
 *
 * B1 is Beads' SEED button and B2 its FREEZE button. Hold B2 and tap B1 to
 * pick the next quality, it applies when B2 is let go. Hold B1 and tap B2 to
 * hand the grains back to Density. Hold both and nudge a pot to send CV2 to
 * time, size, pitch or freeze; hold both for two seconds to learn the input
 * level again.
 *
 * The audio callback only renders. Pots, buttons and the LED live in the
 * main loop, the CV inputs are read once per block in the callback.
 */

#ifndef IN_DTCM
#define IN_DTCM __attribute__((section(".dtcmram_bss")))
#define IN_D2 __attribute__((section(".sram1_bss.grani")))
#define IN_SDRAM __attribute__((section(".sdram_bss")))
#endif

static const size_t kBankSize = 256 * 1024;
static const size_t kMaxBlock = 24;

static Carciofo hw;

// Beads kept all of this in on-chip SRAM. The Seed has less of it in one
// place, so the three delay banks are spread over AXI SRAM, D2 and the SDRAM,
// and the reverb and the processor itself sit in the DTCM.
static uint8_t bank0[kBankSize];
static uint8_t IN_D2 bank1[kBankSize];
static uint8_t IN_SDRAM bank2[kBankSize];
static uint16_t IN_DTCM reverbBuffer[32768];
static Processor IN_DTCM processor;
static GainControl gainControl;

enum Layer { LAYER_MAIN = 0, LAYER_MIX, LAYER_RANDOM, kNumLayers, LAYER_BOTH };
enum Cv2Dest { CV2_TIME = 0, CV2_SIZE, CV2_PITCH, CV2_FREEZE, kNumCv2 };
enum SeedMode { SEED_AUTO = 0, SEED_MANUAL };

static const int kNumPots = 4;

static const uint32_t kTapMs = 300;
static const uint32_t kChordGuardMs = 60;
static const uint32_t kRelockMs = 4000;
static const uint32_t kRelearnMs = 2000;
static const uint32_t kClockTimeoutMs = 30000;
static const uint32_t kMonoMs = 2000;
static const float kGestureEps = 0.03f;
static const float kCatchEps = 0.02f;
static const float kMoveEps = 0.004f;

static const float kGateHigh = 0.25f;
static const float kGateLow = 0.15f;

/* What the main loop hands to the audio callback. */
struct Controls {
  float time, size, pitch, density;
  float feedback, dryWet, reverb, shape;
  float timeRnd, sizeRnd, pitchRnd, shapeRnd;
};

static volatile Controls ctl;
static volatile bool sFrozen = false;
static volatile uint8_t sSeedMode = SEED_AUTO;
static volatile bool sB1Gate = false;
static volatile bool sDelayTapped = false;
static volatile bool sClockPatched = false;
static volatile bool sMonoIn = true;
static volatile uint8_t sCv2 = CV2_PITCH;
static volatile float sFadeTarget = 0.f;
static volatile bool sRelearn = false;

static volatile bool sDelayMode = false;
static volatile bool sFadedOut = true;
static volatile bool sHeal = false;
static volatile bool sCv2Gate = false;
static volatile uint32_t sEdges = 0;
static volatile float sPeakL = 0.f, sPeakR = 0.f;
static volatile float sGovScale = 1.f;
static volatile float sLoad = 0.f;

static float sampleRate = 48000.f;
static float fadeGain = 0.f;
static float delayTransition = -1.5f;
static float cv2Smooth = 0.f;
static bool cv1High = false;
static bool delayedHigh = false;
static bool suppress = false;
static GateFlags previousSeed = stmlib::GATE_FLAG_LOW;
static bool seedRing[16];
static uint32_t seedRingPos = 0;
static uint32_t seedDelayBlocks = 8;
static float ticksPerSample = 1.f;

/* Beads' pot laws, from its cv_reader.h. */
static float AttenuverterLaw(float v) {
  v -= 0.5f;
  return (2.0f / 0.47f) * v * std::max(fabsf(v) - 0.03f, 0.0f);
}

static float DetuneLaw(float v) {
  float detune = 2.0f * v - 1.0f;
  float sign = detune < 0.0f ? -1.0f : 1.0f;
  detune = detune * sign * 6.0f;
  MAKE_INTEGRAL_FRACTIONAL(detune);
  if (detune_integral > 6) detune_integral = 6;
  static const float kIntervals[8] = {0.f, 1.f,  3.f,  4.f,
                                      7.f, 12.f, 24.f, 24.f};
  detune_fractional = std::max((detune_fractional - 0.33333333f) * 1.5f, 0.f);
  float a = kIntervals[detune_integral];
  float b = kIntervals[detune_integral + 1];
  return stmlib::Crossfade(a, b, stmlib::SmoothStep(detune_fractional)) * sign;
}

/* CV2 as pitch: the input stage reads 0 V to +5 V as 0 to 1 with a small
   offset below 38 mV, undone here so the scale is 12 semitones per volt. */
static float Cv2Semitones(float cv) {
  float volts = cv < 0.01f ? cv * 8.722f
                           : (cv * 3.3f + 0.025f) * (1.f / 0.665f);
  return 12.f * volts;
}

static float GovernLoad(float scale, float load) {
  if (load > 0.8f) scale *= 0.95f;
  else if (load < 0.6f) scale += 0.0002f;
  return Clamp(scale, 0.f, 1.f);
}

static void AudioCallback(daisy::AudioHandle::InputBuffer in,
                          daisy::AudioHandle::OutputBuffer out, size_t size) {
  uint32_t t0 = System::GetTick();
  if (size > kMaxBlock) size = kMaxBlock;

  float cv1 = hw.GetCv(CV_1);
  bool rising = false;
  if (!cv1High && cv1 > kGateHigh) {
    cv1High = rising = true;
  } else if (cv1High && cv1 < kGateLow) {
    cv1High = false;
  }
  if (rising) sEdges = sEdges + 1;

  seedRing[seedRingPos & 15] = cv1High;
  bool delayed = seedRing[(seedRingPos - seedDelayBlocks) & 15];
  seedRingPos++;
  if (delayed && !delayedHigh) {
    suppress = stmlib::Random::GetFloat() > sGovScale;
  }
  delayedHigh = delayed;
  bool seedGate = delayed && !suppress;

  float cv2 = hw.GetCv(CV_2);
  cv2Smooth += 0.3f * (cv2 - cv2Smooth);
  uint8_t dest = sCv2;
  if (dest == CV2_FREEZE) {
    if (!sCv2Gate && cv2 > kGateHigh) sCv2Gate = true;
    else if (sCv2Gate && cv2 < kGateLow) sCv2Gate = false;
  } else {
    sCv2Gate = false;
  }

  float peakL = sPeakL, peakR = sPeakR;
  Codec::Frame codecIn[kMaxBlock];
  for (size_t i = 0; i < size; i++) {
    float l = Clamp(in[0][i], -1.f, 0.999999f);
    float r = Clamp(in[1][i], -1.f, 0.999999f);
    peakL = std::max(peakL, fabsf(l));
    peakR = std::max(peakR, fabsf(r));
    codecIn[i].l = static_cast<int32_t>(l * 2147483648.f);
    codecIn[i].r = static_cast<int32_t>(r * 2147483648.f);
  }
  sPeakL = peakL;
  sPeakR = peakR;

  if (sRelearn) {
    sRelearn = false;
    gainControl.Restart();
  }
  FloatFrame floatIn[kMaxBlock];
  FloatFrame floatOut[kMaxBlock];
  gainControl.Process(!sFadedOut, 0, false, sMonoIn, codecIn, floatIn, size);

  Parameters p;
  p.time.base = ctl.time + (dest == CV2_TIME ? cv2Smooth : 0.f);
  p.time.attenuverter = ctl.timeRnd;
  p.time.cv = 0.f;
  p.time.has_cv = false;
  p.size.base = ctl.size;
  if (dest == CV2_SIZE) {
    p.size.base += cv2Smooth;
    if (ctl.size < 0.98f) p.size.base = std::min(p.size.base, 0.975f);
  }
  p.size.attenuverter = ctl.sizeRnd;
  p.size.cv = 0.f;
  p.size.has_cv = false;
  p.shape.base = ctl.shape;
  p.shape.attenuverter = ctl.shapeRnd;
  p.shape.cv = 0.f;
  p.shape.has_cv = false;
  p.pitch.base = ctl.pitch;
  if (dest == CV2_PITCH) p.pitch.base += Cv2Semitones(cv2Smooth);
  p.pitch.attenuverter = ctl.pitchRnd;
  p.pitch.cv = 0.f;
  p.pitch.has_cv = false;
  p.monophonic_mode = false;

  // Mirrors the processor's own switch between granular and delay mode, so
  // the buttons and the LED know which one is playing.
  float threshold = delayTransition > 0.f ? 0.98f : 0.99f;
  delayTransition += (p.size.base > threshold ? 1.f : -1.f) * size / sampleRate;
  delayTransition = Clamp(delayTransition, -1.5f, 1.5f);
  bool delayMode = delayTransition > 0.f;
  sDelayMode = delayMode;

  float governor = sGovScale;
  ModulatedParameter d;
  d.base = ctl.density;
  d.cv = 0.f;
  if (!sClockPatched && !delayMode) {
    d.base = 0.5f + (d.base - 0.5f) * governor;
  }

  Processor::MixParameters m;
  m.feedback = Clamp(ctl.feedback * 1.005f, 0.f, 1.f);
  m.dry_wet = Clamp(ctl.dryWet * 1.005f, 0.f, 1.f);
  m.reverb_amount = Clamp(ctl.reverb * 1.005f, 0.f, 1.f);
  m.output_gate = false;

  SeedButtonState seed;
  if (delayMode) {
    seed = !sDelayTapped ? SEED_BUTTON_STATE_LOCKED
                         : (sB1Gate ? SEED_BUTTON_STATE_ON
                                    : SEED_BUTTON_STATE_OFF);
  } else {
    seed = sSeedMode == SEED_AUTO ? SEED_BUTTON_STATE_LOCKED
                                  : (sB1Gate ? SEED_BUTTON_STATE_ON
                                             : SEED_BUTTON_STATE_OFF);
  }

  GateFlags seedFlags[kMaxBlock];
  const GateFlags* seedInput = NULL;
  bool external = sClockPatched || (!delayMode && sSeedMode == SEED_MANUAL);
  if (external) {
    for (size_t i = 0; i < size; i++) {
      previousSeed = seedFlags[i] =
          stmlib::ExtractGateFlags(previousSeed, seedGate);
    }
    seedInput = seedFlags;
  } else {
    previousSeed = stmlib::GATE_FLAG_LOW;
  }

  bool frozen = sFrozen || sCv2Gate;
  bool freeze[kMaxBlock];
  for (size_t i = 0; i < size; i++) freeze[i] = frozen;

  processor.Process(seed, seedInput, freeze, d, p, m, floatIn, floatOut, size);

  float target = sFadeTarget;
  float fadeStep = 1.f / (0.01f * sampleRate);
  float finite = 0.f;
  for (size_t i = 0; i < size; i++) {
    fadeGain += Clamp(target - fadeGain, -fadeStep, fadeStep);
    float l = floatOut[i].l, r = floatOut[i].r;
    finite += l + r;
    out[0][i] = Clamp(l, -1.f, 0.999999f) * fadeGain;
    out[1][i] = Clamp(r, -1.f, 0.999999f) * fadeGain;
  }
  if (!std::isfinite(finite)) {
    for (size_t i = 0; i < size; i++) out[0][i] = out[1][i] = 0.f;
    fadeGain = 0.f;
    sHeal = true;
  }
  sFadedOut = fadeGain <= 0.f && target <= 0.f;

  float load = (System::GetTick() - t0) / (size * ticksPerSample);
  sLoad = load;
  sGovScale = GovernLoad(governor, load);
  if (load > 0.95f) processor.ShedGrains(2);
}

/* Sets the processor to a quality and starts the codec at the rate that
   quality needs, with the audio stopped. Clears the buffer, as on Beads. */
static void StartEngine(int media, bool first) {
  processor.Configure(Processor::Media(media), false);
  sampleRate = processor.sample_rate();
  hw.seed.SetAudioSampleRate(
      sampleRate < 40000.f ? daisy::SaiHandle::Config::SampleRate::SAI_32KHZ
                           : daisy::SaiHandle::Config::SampleRate::SAI_48KHZ);
  GainControl::SoftClippingMode clip =
      media == Processor::MEDIA_DIRTY_TAPE
          ? GainControl::SOFT_CLIPPING_MODE_ON
          : (media == Processor::MEDIA_CLEAN_TAPE
                 ? GainControl::SOFT_CLIPPING_MODE_DYNAMIC
                 : GainControl::SOFT_CLIPPING_MODE_OFF);
  gainControl.Init(sampleRate, clip, first);
  float blockMs = 16000.f / sampleRate;
  seedDelayBlocks = static_cast<uint32_t>(2.5f / blockMs + 0.5f);
  if (seedDelayBlocks < 1) seedDelayBlocks = 1;
  ticksPerSample = System::GetTickFreq() / sampleRate;
  fadeGain = 0.f;
  sFadedOut = false;
  sFadeTarget = 1.f;
  hw.StartAudio(AudioCallback);
}

/* ---- Main loop ---------------------------------------------------------- */

struct Saved {
  uint8_t media;
  uint8_t cv2;
  float mix[kNumPots];
  float random[kNumPots];
  bool operator==(const Saved& o) const {
    return media == o.media && cv2 == o.cv2 &&
           SameKnobs(mix, o.mix, kNumPots) &&
           SameKnobs(random, o.random, kNumPots);
  }
  bool operator!=(const Saved& o) const { return !(*this == o); }
};

static const Saved kDefaults = {Processor::MEDIA_DIGITAL,
                                CV2_PITCH,
                                {0.f, 0.6f, 0.3f, 0.5f},
                                {0.65f, 0.5f, 0.5f, 0.5f}};

static Settings<Saved> settings;

struct Key {
  bool down = false;
  bool used = false;
  bool combo = false;
  uint32_t downAt = 0;
  float pots[kNumPots];
};

static Key key[2];
static float knob[kNumLayers][kNumPots];
static bool caught[kNumLayers][kNumPots];
static float lastRaw[kNumPots];
static float smooth[kNumLayers][kNumPots];
static Layer layer = LAYER_MAIN;
static int media = Processor::MEDIA_DIGITAL;
static int preview = Processor::MEDIA_DIGITAL;
static int pendingMedia = -1;
static bool freezeByPress = false;
static bool bothActed = false;

static uint32_t lastEdges = 0;
static uint32_t lastEdgeMs = 0;
static bool everClocked = false;
static uint32_t rSilentMs = 0;
static uint32_t lastGrainMs = 0;
static uint32_t lastTapMs = 0;
static uint32_t tapPeriodMs = 0;
static uint32_t tapQueue[4];
static int tapCount = 0;
static uint32_t tapPulseEnd = 0;
static bool wasDelay = false;

static uint32_t flashUntil = 0;
static float flashColour[3];
static uint32_t refuseAt = 0;
static uint32_t catchAt = 0;
static uint32_t moveAt = 0;
static int movedPot = -1;

static const float kMediaColour[4][3] = {
    {1.0f, 0.10f, 0.45f},   // dirty tape
    {1.0f, 0.55f, 0.00f},   // clean tape
    {0.0f, 0.85f, 0.60f},   // vintage digital
    {0.10f, 0.25f, 1.0f}};  // digital
static const float kCv2Colour[kNumCv2][3] = {
    {0.0f, 1.0f, 0.2f}, {1.0f, 0.85f, 0.f}, {0.6f, 0.1f, 1.0f},
    {1.0f, 0.0f, 0.0f}};

static int NextMedia(int m) { return (m + 3) % 4; }

static void Flash(const float* c, uint32_t now, uint32_t ms = 150) {
  for (int i = 0; i < 3; i++) flashColour[i] = c[i];
  flashUntil = now + ms;
}

static bool PotsMoved(const Key& b) {
  for (int i = 0; i < kNumPots; i++) {
    if (fabsf(hw.GetPot(static_cast<Pot>(i)) - b.pots[i]) > kGestureEps) {
      return true;
    }
  }
  return false;
}

static void Press(int which, uint32_t now) {
  Key& b = key[which];
  Key& other = key[1 - which];
  b.down = true;
  b.used = false;
  b.combo = false;
  b.downAt = now;
  for (int i = 0; i < kNumPots; i++) b.pots[i] = hw.GetPot(static_cast<Pot>(i));

  if (other.down) {
    if (freezeByPress) {
      sFrozen = false;
      freezeByPress = false;
    }
    if (now - other.downAt < kChordGuardMs) {
      b.used = other.used = true;
    } else {
      b.combo = true;
      other.used = true;
    }
    if (sB1Gate) sB1Gate = false;
    bothActed = false;
    return;
  }

  if (which == BUTTON_2) {
    if (!sFrozen && !sCv2Gate) {
      sFrozen = true;
      freezeByPress = true;
    }
    preview = media;
  } else if (!sDelayMode && sSeedMode == SEED_MANUAL) {
    sB1Gate = true;
    lastGrainMs = now;
  }
}

static void Release(int which, uint32_t now) {
  Key& b = key[which];
  Key& other = key[1 - which];
  b.down = false;
  bool clean = !b.used && now - b.downAt < kTapMs && !PotsMoved(b);

  if (which == BUTTON_1) sB1Gate = false;

  if (b.combo) {
    if (clean && other.down) {
      if (which == BUTTON_1) {
        if (sFrozen || sCv2Gate) {
          refuseAt = now;
        } else {
          preview = NextMedia(preview);
          Flash(kMediaColour[preview], now, 120);
        }
      } else {
        sSeedMode = SEED_AUTO;
        sDelayTapped = false;
        tapPeriodMs = 0;
        static const float kWhite[3] = {1.f, 1.f, 1.f};
        Flash(kWhite, now);
      }
    }
    if (other.down) other.used = true;
  } else if (which == BUTTON_2) {
    if (freezeByPress) {
      if (!clean) sFrozen = false;
    } else if (clean) {
      sFrozen = false;
    }
    freezeByPress = false;
    if (preview != media) pendingMedia = preview;
  } else if (clean && sDelayMode) {
    if (tapCount < 4) tapQueue[tapCount++] = b.downAt + kTapMs;
  } else if (clean && sSeedMode == SEED_AUTO) {
    sSeedMode = SEED_MANUAL;
  }

  if (other.down) other.used = true;
  b.combo = false;
}

static void ReadButtons(uint32_t now) {
  for (int i = 0; i < 2; i++) {
    if (hw.button[i].RisingEdge()) Press(i, now);
    if (hw.button[i].FallingEdge()) Release(i, now);
  }
  Key& b1 = key[BUTTON_1];
  Key& b2 = key[BUTTON_2];

  // A delay tap is only known to be a tap once B1 is let go, so it plays a
  // fixed time after its press: the spacing of the presses stays the tempo.
  if (tapPulseEnd && now >= tapPulseEnd) {
    sB1Gate = false;
    tapPulseEnd = 0;
  } else if (tapCount && !tapPulseEnd && now >= tapQueue[0]) {
    uint32_t press = tapQueue[0] - kTapMs;
    if (sDelayMode && !sDelayTapped) {
      sDelayTapped = true;
      lastTapMs = 0;
    } else {
      for (int i = 1; i < tapCount; i++) tapQueue[i - 1] = tapQueue[i];
      tapCount--;
      if (sDelayMode) {
        if (lastTapMs && press - lastTapMs > 60 && press - lastTapMs < 4000) {
          tapPeriodMs = press - lastTapMs;
        }
        lastTapMs = press;
        sB1Gate = true;
        tapPulseEnd = now + 3;
      }
    }
  }

  if (b1.down && sB1Gate && PotsMoved(b1)) sB1Gate = false;
  bool streaming = !sDelayMode && sSeedMode == SEED_MANUAL &&
                   fabsf(ctl.density - 0.5f) > 0.02f;
  if (b1.down && !b2.down && !b1.used && !streaming &&
      now - b1.downAt >= kRelockMs && !PotsMoved(b1)) {
    b1.used = true;
    sB1Gate = false;
    sSeedMode = SEED_AUTO;
    sDelayTapped = false;
    tapPeriodMs = 0;
    static const float kWhite[3] = {1.f, 1.f, 1.f};
    Flash(kWhite, now);
  }

  if (b1.down && b2.down && !bothActed) {
    Key& later = b1.downAt > b2.downAt ? b1 : b2;
    for (int i = 0; i < kNumPots; i++) {
      if (fabsf(hw.GetPot(static_cast<Pot>(i)) - later.pots[i]) >
          kGestureEps) {
        sCv2 = static_cast<uint8_t>(i);
        bothActed = true;
        b1.used = b2.used = true;
        Flash(kCv2Colour[i], now, 300);
        break;
      }
    }
    if (!bothActed && now - later.downAt >= kRelearnMs) {
      bothActed = true;
      b1.used = b2.used = true;
      sRelearn = true;
      static const float kWhite[3] = {1.f, 1.f, 1.f};
      Flash(kWhite, now, 300);
    }
  }
  if (!b1.down && !b2.down) bothActed = false;
}

/* The soft pickup, as in Quattro: a pot that has not been caught since its
   layer was entered leaves the stored value alone until it is turned back
   through it. */
static void ReadPots(uint32_t now) {
  Layer next = key[BUTTON_1].down && key[BUTTON_2].down
                   ? LAYER_BOTH
                   : (key[BUTTON_1].down
                          ? LAYER_RANDOM
                          : (key[BUTTON_2].down ? LAYER_MIX : LAYER_MAIN));
  if (next != layer) {
    layer = next;
    if (layer != LAYER_BOTH) {
      for (int i = 0; i < kNumPots; i++) {
        caught[layer][i] =
            fabsf(hw.GetPot(static_cast<Pot>(i)) - knob[layer][i]) < kCatchEps;
      }
    }
  }

  float best = kMoveEps;
  for (int i = 0; i < kNumPots; i++) {
    float raw = hw.GetPot(static_cast<Pot>(i));
    if (layer != LAYER_BOTH) {
      float stored = knob[layer][i];
      if (!caught[layer][i]) {
        bool crossed = (raw - stored) * (lastRaw[i] - stored) < 0.f;
        if (fabsf(raw - stored) < kCatchEps || crossed) {
          caught[layer][i] = true;
          catchAt = now;
        }
      }
      if (caught[layer][i]) knob[layer][i] = raw;
      float moved = fabsf(raw - lastRaw[i]);
      if (moved > best) {
        best = moved;
        movedPot = i;
        moveAt = now;
      }
    }
    lastRaw[i] = raw;
  }

  for (int l = 0; l < kNumLayers; l++) {
    for (int i = 0; i < kNumPots; i++) {
      smooth[l][i] += 0.05f * (knob[l][i] - smooth[l][i]);
    }
  }
  float time = smooth[LAYER_MAIN][0];
  if (fabsf(time - ctl.time) > 0.001f) ctl.time = time;
  ctl.size = smooth[LAYER_MAIN][1];
  ctl.pitch = DetuneLaw(smooth[LAYER_MAIN][2]);
  ctl.density = smooth[LAYER_MAIN][3];
  ctl.feedback = smooth[LAYER_MIX][0];
  ctl.dryWet = smooth[LAYER_MIX][1];
  ctl.reverb = smooth[LAYER_MIX][2];
  ctl.shape = smooth[LAYER_MIX][3];
  ctl.timeRnd = AttenuverterLaw(smooth[LAYER_RANDOM][0]);
  ctl.sizeRnd = AttenuverterLaw(smooth[LAYER_RANDOM][1]);
  ctl.pitchRnd = AttenuverterLaw(smooth[LAYER_RANDOM][2]);
  ctl.shapeRnd = AttenuverterLaw(smooth[LAYER_RANDOM][3]);
}

static void WatchInputs(uint32_t now) {
  uint32_t edges = sEdges;
  if (edges != lastEdges) {
    lastEdges = edges;
    lastEdgeMs = now;
    everClocked = true;
    lastGrainMs = now;
  }
  sClockPatched = everClocked && now - lastEdgeMs < kClockTimeoutMs;

  float peakL = sPeakL, peakR = sPeakR;
  sPeakL = sPeakR = 0.f;
  if (!sFrozen) {
    if (peakR > 0.00316f) {
      sMonoIn = false;
      rSilentMs = 0;
    } else if (peakR < 0.0005f && peakL > 0.00316f) {
      if (++rSilentMs >= kMonoMs) sMonoIn = true;
    }
  }

  bool delay = sDelayMode;
  if (delay && !wasDelay) {
    static const float kWhite[3] = {1.f, 1.f, 1.f};
    Flash(kWhite, now, 200);
  }
  wasDelay = delay;

  hw.seed.SetLed(sGovScale < 0.99f);
}

static bool healing = false;

static void InitProcessor() {
  uint8_t* banks[3] = {bank0, bank1, bank2};
  processor.Init(banks, reverbBuffer);
  delayTransition = -1.5f;
}

/* A quality change, or a restart after the output went non-finite. The
   restart rebuilds the whole processor, its smoothed mix levels included. */
static void ApplyQuality() {
  if (sHeal && pendingMedia < 0) {
    sHeal = false;
    healing = true;
    pendingMedia = media;
  }
  if (pendingMedia < 0) return;
  sFadeTarget = 0.f;
  if (!sFadedOut) return;
  media = pendingMedia;
  preview = media;
  pendingMedia = -1;
  sDelayTapped = false;
  tapPeriodMs = 0;
  tapCount = 0;
  hw.seed.StopAudio();
  if (healing) InitProcessor();
  healing = false;
  StartEngine(media, false);
}

/* ---- LED ---------------------------------------------------------------- */

static float Breath(uint32_t now, uint32_t period) {
  float phase = (now % period) / static_cast<float>(period);
  return 0.5f - 0.5f * cosf(6.2831853f * phase);
}

static void UpdateLed(uint32_t now) {
  const float* hue = kMediaColour[media];
  float r, g, b;

  if (now < flashUntil) {
    hw.led.Set(flashColour[0], flashColour[1], flashColour[2]);
    return;
  }
  if (refuseAt && now - refuseAt < 320) {
    uint32_t t = now - refuseAt;
    float v = (t < 80 || (t >= 160 && t < 240)) ? 0.f : 0.6f;
    hw.led.Set(hue[0] * v, hue[1] * v, hue[2] * v);
    return;
  }
  if (now - catchAt < 60) {
    hw.led.Set(1.f, 1.f, 1.f);
    return;
  }

  if (layer == LAYER_BOTH) {
    const float* c = kCv2Colour[sCv2];
    hw.led.Set(c[0] * 0.7f, c[1] * 0.7f, c[2] * 0.7f);
    return;
  }
  if (layer != LAYER_MAIN) {
    bool hunting = movedPot >= 0 && now - moveAt < 400 &&
                   !caught[layer][movedPot];
    if (hunting && (now / 60) % 2) {
      hw.led.Set(0.f, 0.f, 0.f);
      return;
    }
    const float* c = hue;
    if (layer == LAYER_MIX && preview != media) {
      c = kMediaColour[preview];
      float v = 0.3f + 0.6f * Breath(now, 500);
      hw.led.Set(c[0] * v, c[1] * v, c[2] * v);
      return;
    }
    float v = layer == LAYER_RANDOM ? 0.35f + 0.35f * Breath(now, 1200) : 0.6f;
    r = 0.7f * v + 0.3f * v * c[0];
    g = 0.7f * v + 0.3f * v * c[1];
    b = 0.7f * v + 0.3f * v * c[2];
    hw.led.Set(r, g, b);
    return;
  }
  if (movedPot >= 0 && now - moveAt < 400 && !caught[LAYER_MAIN][movedPot] &&
      (now / 60) % 2) {
    hw.led.Set(0.f, 0.f, 0.f);
    return;
  }

  float v;
  if (sFrozen || sCv2Gate) {
    v = 1.f;
  } else if (sDelayMode) {
    v = 0.35f;
    uint32_t period = sClockPatched ? 0 : tapPeriodMs;
    if (sClockPatched) {
      if (now - lastGrainMs < 40) v = 0.9f;
    } else if (period && lastTapMs) {
      if ((now - lastTapMs) % period < 40) v = 0.9f;
    } else if (now % 1500 < 120) {
      v = 0.f;
    }
  } else if (sSeedMode == SEED_MANUAL) {
    v = now - lastGrainMs < 40 ? 0.8f : 0.03f;
  } else {
    bool dead = !sClockPatched && fabsf(ctl.density - 0.5f) <= 0.02f;
    v = dead ? 0.06f : 0.08f + 0.27f * Breath(now, 4000);
    if (sClockPatched && now - lastGrainMs < 30) v = 0.7f;
  }
  hw.led.Set(hue[0] * v, hue[1] * v, hue[2] * v);
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

static void Setup() {
  memset(bank0, 0, sizeof(bank0));
  memset(bank1, 0, sizeof(bank1));
  memset(bank2, 0, sizeof(bank2));
  memset(reverbBuffer, 0, sizeof(reverbBuffer));
  memset(static_cast<void*>(&processor), 0, sizeof(processor));
  InitTables();
  InitProcessor();

  settings.Init(hw, SLOT_GRANI, 1, kDefaults);
  const Saved& saved = settings.Get();
  media = saved.media < 4 ? saved.media : kDefaults.media;
  preview = media;
  sCv2 = saved.cv2 < kNumCv2 ? saved.cv2 : kDefaults.cv2;
  for (int i = 0; i < kNumPots; i++) {
    float raw = hw.GetPot(static_cast<Pot>(i));
    knob[LAYER_MAIN][i] = raw;
    knob[LAYER_MIX][i] = ValidKnob(saved.mix[i], kDefaults.mix[i]);
    knob[LAYER_RANDOM][i] = ValidKnob(saved.random[i], kDefaults.random[i]);
    caught[LAYER_MAIN][i] = true;
    lastRaw[i] = raw;
    for (int l = 0; l < kNumLayers; l++) smooth[l][i] = knob[l][i];
  }
  ReadPots(0);
}

static void ControlPass() {
  hw.ProcessControls();
  uint32_t now = System::GetNow();
  ReadButtons(now);
  ReadPots(now);
  WatchInputs(now);
  ApplyQuality();
  UpdateLed(now);

  Saved s;
  s.media = static_cast<uint8_t>(media);
  s.cv2 = sCv2;
  for (int i = 0; i < kNumPots; i++) {
    s.mix[i] = knob[LAYER_MIX][i];
    s.random[i] = knob[LAYER_RANDOM][i];
  }
  if (s != settings.Get()) settings.Get() = s;
  settings.Process();
}

int main(void) {
  hw.Init(16, true);
  Setup();
  RunBootSweep();
  StartEngine(media, true);
  while (1) {
    ControlPass();
    System::Delay(1);
  }
}
