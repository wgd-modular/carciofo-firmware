#include "../../lib/carciofo.h"
#include "daisysp.h"

using namespace daisy;
using namespace daisysp;
using namespace carciofo;

/*
 * Bring up firmware, checks every control on the panel without a scope.
 *
 * On power up the LED runs through red, green, blue and white so all three
 * channels get proven. After that it tracks one analog control at a time:
 * every control has its own colour and the brightness follows the value, so
 * turning the selected pot fades the LED up. B1 steps through the six
 * controls, B2 swaps the audio path between the input jacks and a pair of
 * test tones and lights the seed's onboard LED while they run.
 *
 * The same readings go out over USB serial four times a second if you want
 * the actual numbers.
 */

static const int kNumControls = 6;
static const char* kControlNames[kNumControls] = {"P1", "P2",  "P3",
                                                  "P4", "CV1", "CV2"};
static const float kControlHues[kNumControls] = {0.00f, 0.08f, 0.33f,
                                                 0.50f, 0.66f, 0.83f};

static Carciofo hw;
static Oscillator toneLeft, toneRight;

static int selected;
static bool tonesEnabled;

static float ReadControl(int index) {
  return index < 4 ? hw.GetPot(static_cast<Pot>(index))
                   : hw.GetCv(static_cast<Cv>(index));
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  for (size_t i = 0; i < size; i++) {
    if (tonesEnabled) {
      out[0][i] = toneLeft.Process();
      out[1][i] = toneRight.Process();
    } else {
      out[0][i] = in[0][i];
      out[1][i] = in[1][i];
    }
  }
}

static void RunLedSelfTest() {
  static const float steps[][3] = {
      {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f, 1.f}};

  for (size_t i = 0; i < sizeof(steps) / sizeof(steps[0]); i++) {
    hw.led.Set(steps[i][0], steps[i][1], steps[i][2]);
    hw.led.Update();
    System::Delay(250);
  }
}

int main(void) {
  hw.Init();
  hw.seed.StartLog();

  toneLeft.Init(hw.SampleRate());
  toneLeft.SetWaveform(Oscillator::WAVE_SIN);
  toneLeft.SetFreq(440.f);
  toneLeft.SetAmp(0.5f);

  toneRight.Init(hw.SampleRate());
  toneRight.SetWaveform(Oscillator::WAVE_SIN);
  toneRight.SetFreq(660.f);
  toneRight.SetAmp(0.5f);

  RunLedSelfTest();
  hw.StartAudio(AudioCallback);

  uint32_t lastPrint = System::GetNow();

  while (1) {
    hw.ProcessControls();

    if (hw.button[BUTTON_1].RisingEdge()) {
      selected = (selected + 1) % kNumControls;
      hw.seed.PrintLine("watching %s", kControlNames[selected]);
    }

    if (hw.button[BUTTON_2].RisingEdge()) {
      tonesEnabled = !tonesEnabled;
      hw.seed.SetLed(tonesEnabled);
      hw.seed.PrintLine("test tones %s", tonesEnabled ? "on" : "off");
    }

    // Keep a little light on at zero so the selected colour stays readable.
    hw.led.SetHsv(kControlHues[selected], 1.f,
                  0.05f + 0.95f * ReadControl(selected));

    uint32_t now = System::GetNow();
    if (now - lastPrint >= 250) {
      lastPrint = now;
      hw.seed.PrintLine("P1 " FLT_FMT3 " P2 " FLT_FMT3 " P3 " FLT_FMT3
                        " P4 " FLT_FMT3 " CV1 " FLT_FMT3 " CV2 " FLT_FMT3
                        " B1 %d B2 %d",
                        FLT_VAR3(hw.GetPot(POT_1)), FLT_VAR3(hw.GetPot(POT_2)),
                        FLT_VAR3(hw.GetPot(POT_3)), FLT_VAR3(hw.GetPot(POT_4)),
                        FLT_VAR3(hw.GetCv(CV_1)), FLT_VAR3(hw.GetCv(CV_2)),
                        hw.button[BUTTON_1].Pressed() ? 1 : 0,
                        hw.button[BUTTON_2].Pressed() ? 1 : 0);
    }
  }
}
