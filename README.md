# carciofo-firmware

A repository to collect different firmwares for the daisy-based Carciofo module, the 6hp successor of the [Löwenzahnhonig](https://github.com/wgd-modular/loewenzahnhonig-firmware). Same four pots and two CV inputs, plus two buttons and an RGB LED.


## Pre-compiled firmware

All firmwares are pre-compiled into binaries that are ready to be uploaded to the Daisy Seed. You can find them attached to the [latest release](https://github.com/wgd-modular/carciofo-firmware/releases/latest). Simply open the [Daisy Web Flasher](https://electro-smith.github.io/Programmer/) via Google Chrome and upload the `.bin` file and flash your seed.


## Controls

One side of the modules front panel has a generic labelling. This table contains a mapping of which control is associated with which pin of the daisy seed.

| **Control** | **Daisy Pin** |
|------------ | ------------- |
| P1          | A0            |
| P2          | A1            |
| P3          | A2            |
| P4          | A3            |
| CV1         | A4            |
| CV2         | A5            |
| B1          | D6            |
| B2          | D7            |
| LED         | D5            |


# Compilation

This step is only required if you want to modify a firmware or create your own. The firmwares depend on the `libDaisy` and `DaisySP` development libraries by Electro-Smith. These are added as submodules to this repository and are most easily fetched when initially cloning the repository as follows:

```shell
git clone --recursive https://github.com/wgd-modular/carciofo-firmware
```

Next you run the provided script to compile the libraries:

```shell
./build_libs.sh
```

Finally, if you want to compile all firmwares you can run the provided Python script:

```shell
python ./build_firmwares.py
```

If instead you want to compile one specific firmware, you should change your working directory to that of the firmware and execute the Makefile. For example:

```shell
cd src/reverb
make
```

This will compile the reverb firmware and place its output in the `build` subdirectory.


## Writing a firmware

`lib/carciofo.h` covers the panel so that a firmware does not have to repeat the ADC setup, the button debouncing and the LED timing every time:

```cpp
#include "../../lib/carciofo.h"

using namespace daisy;
using namespace carciofo;

Carciofo hw;
float gain;

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out,
                   size_t size) {
  for (size_t i = 0; i < size; i++) {
    out[0][i] = in[0][i] * gain;
    out[1][i] = in[1][i] * gain;
  }
}

int main(void) {
  hw.Init();
  hw.StartAudio(AudioCallback);

  while (1) {
    hw.ProcessControls();
    gain = hw.button[BUTTON_1].Pressed() ? 0.f : hw.GetPotWithCv(POT_1, CV_1);
    hw.led.SetHsv(0.33f, 1.f, gain);
    System::Delay(1);
  }
}
```

`ProcessControls()` debounces both buttons and pushes the staged LED colour, so it belongs at the top of the main loop. `GetPot()` and `GetCv()` hand back 0 to 1, `GetPotWithCv()` sums the two and clamps. The buttons are plain libDaisy `Switch` objects, the LED takes either RGB or HSV.


## Contributing

New firmwares for the Carciofo module are always very welcome. Feel free to open a PR anytime. Each new firmware should be located in a new sub-folder with the same name as the `.cpp` file together with a `README.md` file telling users about the controls and other possibly interesting stuff.
