#include "tables.h"

#include <cmath>

#include "beads/dsp/mu_law.h"
#include "beads/resources.h"
#include "stmlib/dsp/units.h"

namespace beads {

float lut_sine[LUT_SINE_SIZE];
float lut_ulaw_f[256];

}  // namespace beads

namespace stmlib {

float lut_pitch_ratio_high[257];
float lut_pitch_ratio_low[257];

}  // namespace stmlib

void InitTables() {
  for (int i = 0; i < LUT_SINE_SIZE; ++i) {
    beads::lut_sine[i] = sinf(6.283185307179586f * i / 1024.0f);
  }

  // The decoder in the comment of Beads' mu_law.cc.
  for (int i = 0; i < 256; ++i) {
    uint8_t u = ~static_cast<uint8_t>(i);
    int16_t t = ((u & 0xf) << 3) + 0x84;
    t <<= (static_cast<unsigned>(u) & 0x70) >> 4;
    int16_t pcm = (u & 0x80) ? (0x84 - t) : (t - 0x84);
    beads::lut_ulaw_f[i] = static_cast<float>(pcm) / 32768.0f;
  }

  // 2^((i - 128) / 12) and 2^(i / 3072), as in stmlib's units.cc.
  double high = 6.151958251439813e-04;
  double low = 1.0;
  for (int i = 0; i < 257; ++i) {
    stmlib::lut_pitch_ratio_high[i] = static_cast<float>(high);
    stmlib::lut_pitch_ratio_low[i] = static_cast<float>(low);
    high *= 1.0594630943592953;
    low *= 1.0002256593050698;
  }
}
