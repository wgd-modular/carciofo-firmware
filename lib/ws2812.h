#pragma once

#include <cmath>

#include "daisy_seed.h"
#include "util/hal_map.h"
#include "util/scopedirqblocker.h"

namespace carciofo {

/*
 * Driver for the single WS2812B behind the front panel.
 *
 * D5 has neither a timer channel nor an SPI peripheral behind it, so the line
 * is clocked out by hand. libDaisy keeps TIM2 running as a free running
 * counter, but a call to System::GetTick() costs about as much as the pulses
 * we are trying to time, so the busy waits read the counter register directly
 * and the pin is driven through BSRR.
 *
 * A frame is 24 bits of 1.25us and runs with interrupts off. The WS2812B
 * latches whenever the line idles for more than ~50us, so an audio interrupt
 * landing mid frame would tear it in half and leave a random colour behind.
 * 30us of jitter on the audio callback is the cheaper problem.
 */
class Ws2812 {
 public:
  void Init(daisy::Pin pin) {
    gpio_.Init(pin, daisy::GPIO::Mode::OUTPUT, daisy::GPIO::Pull::NOPULL,
               daisy::GPIO::Speed::VERY_HIGH);

    port_ = daisy::GetHALPort(pin);
    pin_high_ = daisy::GetHALPin(pin);
    pin_low_ = pin_high_ << 16;  // BSRR resets from the upper half word

    const uint32_t per_us = daisy::System::GetTickFreq() / 1000000;
    t0h_ = per_us * 400 / 1000;
    t1h_ = per_us * 800 / 1000;
    bit_ = per_us * 1250 / 1000;
    latch_ = per_us * 60;

    colour_ = 0;
    last_sent_ = 0;
    last_send_time_ = 0;
    Send(0);
  }

  /** Stages a colour, components run from 0 to 1. */
  void Set(float r, float g, float b) {
    colour_ = Encode(g) << 16 | Encode(r) << 8 | Encode(b);
  }

  /** Stages a colour given as hue, saturation and value, all from 0 to 1. */
  void SetHsv(float h, float s, float v) {
    Set(Channel(h, s, v, 5.f), Channel(h, s, v, 3.f), Channel(h, s, v, 1.f));
  }

  /** Pushes the staged colour out if it changed. Capped at 200 frames per
      second, so this is cheap enough to call from the main loop. */
  void Update() {
    const uint32_t now = daisy::System::GetNow();
    if (colour_ == last_sent_ || now - last_send_time_ < kMinFrameMs) return;
    last_send_time_ = now;
    Send(colour_);
  }

 private:
  static const uint32_t kMinFrameMs = 5;

  static float Channel(float h, float s, float v, float offset) {
    const float k = fmodf(offset + (h - floorf(h)) * 6.f, 6.f);
    const float f = fminf(k, fminf(4.f - k, 1.f));
    return v - v * s * (f < 0.f ? 0.f : f);
  }

  /* Perceived brightness is nowhere near linear, squaring the input gets a
     linear fade to actually look like one. */
  static uint32_t Encode(float v) {
    if (v <= 0.f) return 0;
    if (v >= 1.f) return 255;
    return static_cast<uint32_t>(v * v * 255.f + 0.5f);
  }

  static uint32_t Ticks() { return TIM2->CNT; }

  static void WaitUntil(uint32_t start, uint32_t ticks) {
    while (Ticks() - start < ticks) {
    }
  }

  void Send(uint32_t grb) {
    {
      daisy::ScopedIrqBlocker blocker;
      for (int i = 23; i >= 0; i--) {
        const uint32_t start = Ticks();
        port_->BSRR = pin_high_;
        WaitUntil(start, (grb >> i) & 1 ? t1h_ : t0h_);
        port_->BSRR = pin_low_;
        WaitUntil(start, bit_);
      }
    }
    WaitUntil(Ticks(), latch_);
    last_sent_ = grb;
  }

  daisy::GPIO gpio_;
  GPIO_TypeDef* port_;
  uint32_t pin_high_, pin_low_;
  uint32_t t0h_, t1h_, bit_, latch_;
  uint32_t colour_, last_sent_, last_send_time_;
};

}  // namespace carciofo
