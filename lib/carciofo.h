#pragma once

#include "daisy_seed.h"
#include "ws2812.h"

/*
 * Hardware layer for the Carciofo. One side of the front panel carries generic
 * labels, this is how they are wired to the seed:
 *
 *   P1 - P4    ADC on pins 15 - 18
 *   CV1, CV2   ADC on pins 19, 20
 *   B1, B2     D6, D7, to ground, internal pull up
 *   LED        D5
 */
namespace carciofo {

enum Pot { POT_1 = 0, POT_2 = 1, POT_3 = 2, POT_4 = 3 };
enum Cv { CV_1 = 4, CV_2 = 5 };
enum Button { BUTTON_1 = 0, BUTTON_2 = 1 };

inline float Clamp(float value, float lower, float upper) {
  return value < lower ? lower : (value > upper ? upper : value);
}

class Carciofo {
 public:
  /** The default block size leaves room for the bit banged LED, which holds
      interrupts off for about 30us whenever it refreshes. */
  void Init(size_t block_size = 16) {
    seed.Configure();
    seed.Init();
    seed.SetAudioBlockSize(block_size);

    daisy::AdcChannelConfig adc[kNumAnalogControls];
    for (size_t i = 0; i < kNumAnalogControls; i++) {
      adc[i].InitSingle(seed.GetPin(kFirstAdcPin + i));
    }
    seed.adc.Init(adc, kNumAnalogControls);
    seed.adc.Start();

    button[BUTTON_1].Init(daisy::seed::D6);
    button[BUTTON_2].Init(daisy::seed::D7);

    led.Init(daisy::seed::D5);
  }

  /** Debounces both buttons and pushes the staged LED colour. Belongs at the
      top of the main loop. */
  void ProcessControls() {
    button[BUTTON_1].Debounce();
    button[BUTTON_2].Debounce();
    led.Update();
  }

  float GetPot(Pot pot) { return seed.adc.GetFloat(pot); }

  /** The CV inputs are inverted by their input stage. */
  float GetCv(Cv cv) { return 1.f - seed.adc.GetFloat(cv); }

  /** A pot with its CV input summed on top, clamped to 0..1. */
  float GetPotWithCv(Pot pot, Cv cv) {
    return Clamp(GetPot(pot) + GetCv(cv), 0.f, 1.f);
  }

  void StartAudio(daisy::AudioHandle::AudioCallback callback) {
    seed.StartAudio(callback);
  }

  float SampleRate() { return seed.AudioSampleRate(); }

  daisy::DaisySeed seed;
  daisy::Switch button[2];
  Ws2812 led;

 private:
  static const size_t kNumAnalogControls = 6;
  static const uint8_t kFirstAdcPin = 15;
};

}  // namespace carciofo
