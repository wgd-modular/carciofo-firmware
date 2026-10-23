#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "carciofo.h"

/*
 * Settings that survive a power cycle, kept in the Seed's QSPI flash.
 *
 * Every firmware owns a slot of its own, so switching firmwares leaves the
 * settings of the others alone. A new firmware adds itself to SettingsSlot.
 * Bump the version passed to Init() whenever the saved struct changes
 * meaning; records of another version or size are ignored and the defaults
 * are used instead.
 *
 * Saving is automatic: Process() writes the settings once they have stopped
 * changing for two seconds. Each save is appended as a record with a sequence
 * number and a checksum, and at power up the newest intact record wins, so a
 * save cut short by switching the case off falls back to the one before.
 * Used sectors are erased at power up, before the audio runs, so a save
 * during play is a page write; only a session with hundreds of saves has to
 * erase on the way.
 *
 * Holding both buttons while powering up brings back the defaults. A button
 * held at power up is ignored until it is let go, so it never counts as a
 * press.
 *
 * The saved struct needs an operator==. Compare pot values with SameKnob(),
 * so ADC noise never counts as a change.
 */
namespace carciofo {

enum SettingsSlot {
  SLOT_TOCCO = 0,
  SLOT_ACCIAIO,
  SLOT_DOPPIO,
  SLOT_ECO,
  SLOT_NEMBO,
  SLOT_QUATTRO,
  SLOT_REVERB,
};

static const float kKnobEps = 0.01f;

inline bool SameKnob(float a, float b) { return fabsf(a - b) <= kKnobEps; }

inline bool SameKnobs(const float* a, const float* b, int n) {
  for (int i = 0; i < n; i++)
    if (!SameKnob(a[i], b[i])) return false;
  return true;
}

/* A pot value read back from the flash, or the fallback if it is not one. */
inline float ValidKnob(float v, float fallback) {
  return v >= 0.f && v <= 1.f ? v : fallback;
}

template <typename T>
class Settings {
 public:
  /* Loads the newest saved settings, or the defaults if there are none or
     both buttons are held, and gets the flash ready for the next saves. Call
     after hw.Init() and before the audio starts. */
  void Init(Carciofo& hw, SettingsSlot slot, uint16_t version,
            const T& defaults) {
    qspi_ = &hw.seed.qspi;
    base_ = kBase + slot * kSectors * kSector;
    version_ = version;
    data_ = defaults;

    uint8_t* mem = static_cast<uint8_t*>(qspi_->GetData(base_));
    dsy_dma_invalidate_cache_for_buffer(mem, kSectors * kSector);

    // Find the newest intact record, and in each sector where the records end.
    int newest = -1;
    uint32_t end[kSectors] = {};
    for (int s = 0; s < kSectors; s++) {
      for (uint32_t off = 0; off < kSector; off += kRecord) {
        const uint8_t* rec = mem + s * kSector + off;
        if (Blank(rec)) continue;
        end[s] = off + kRecord;
        Header h;
        memcpy(&h, rec, sizeof(h));
        T d;
        memcpy(&d, rec + sizeof(h), sizeof(T));
        if (h.magic != kMagic || h.version != version_ ||
            h.size != sizeof(T) || h.checksum != Checksum(h, d))
          continue;
        if (newest < 0 || h.sequence > sequence_) {
          newest = s;
          sequence_ = h.sequence;
          data_ = d;
        }
      }
    }

    // Saves go on in the sector of the newest record. Every other sector in
    // use is erased now, while the audio is not running yet.
    current_ = newest < 0 ? 0 : newest;
    offset_ = newest < 0 ? 0 : end[current_];
    for (int s = 0; s < kSectors; s++) {
      erased_[s] = end[s] == 0;
      if (s != newest && !erased_[s]) Erase(s);
    }
    if (offset_ + kRecord > kSector) {
      // Full: carry the newest record over, then free its old sector.
      int full = current_;
      Append(data_);
      Erase(full);
    }
    saved_ = data_;

    if (HeldAtPowerUp(hw)) {
      data_ = defaults;
      if (Append(data_)) saved_ = data_;
    }
    pending_ = data_;
    changed_ = daisy::System::GetNow();
  }

  /* The settings. Read them once after Init(), then keep them up to date in
     the main loop so Process() can save them. */
  T& Get() { return data_; }

  /* Saves the settings once they have settled. Call every main loop pass. */
  void Process() {
    uint32_t now = daisy::System::GetNow();
    if (!(data_ == pending_)) {
      pending_ = data_;
      changed_ = now;
    }
    if (!(pending_ == saved_) && now - changed_ >= kSaveDelayMs) {
      if (Append(pending_))
        saved_ = pending_;
      else
        changed_ = now;  // try again in a moment
    }
  }

 private:
  struct Header {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint32_t checksum;
  };

  // Records are a power of two, so they line up with the 256 byte pages.
  static constexpr uint32_t RecordSize() {
    uint32_t size = 32;
    while (size < sizeof(Header) + sizeof(T)) size *= 2;
    return size;
  }

  static constexpr uint32_t kBase = 0x700000;
  static constexpr int kSectors = 8;
  static constexpr uint32_t kSector = 4096;
  static constexpr uint32_t kRecord = RecordSize();
  static constexpr uint32_t kMagic = 0x4f494341;  // "ACIO"
  static constexpr uint32_t kSaveDelayMs = 2000;
  static_assert(kRecord <= 256, "Settings must fit in one flash page");
  static_assert(std::is_trivially_copyable<T>::value,
                "Settings are stored byte for byte");

  static bool Blank(const uint8_t* rec) {
    for (uint32_t i = 0; i < kRecord; i++)
      if (rec[i] != 0xff) return false;
    return true;
  }

  static uint32_t Checksum(const Header& h, const T& d) {
    uint32_t x = 2166136261u;  // FNV-1a
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&h);
    for (size_t i = 0; i < offsetof(Header, checksum); i++)
      x = (x ^ p[i]) * 16777619u;
    p = reinterpret_cast<const uint8_t*>(&d);
    for (size_t i = 0; i < sizeof(T); i++) x = (x ^ p[i]) * 16777619u;
    return x;
  }

  /* Whether both buttons are held at power up. Either way, a held button is
     waited out and its release swallowed, so the firmware never sees it. */
  static bool HeldAtPowerUp(Carciofo& hw) {
    daisy::System::Delay(100);  // let the pull ups settle
    Poll(hw, 20);
    bool both =
        hw.button[BUTTON_1].Pressed() && hw.button[BUTTON_2].Pressed();
    if (both) hw.led.Set(1.f, 1.f, 1.f);  // lit until they are let go
    // A stuck button gives up after ten seconds rather than never booting.
    for (int ms = 0; ms < 10000 && (hw.button[BUTTON_1].Pressed() ||
                                    hw.button[BUTTON_2].Pressed());
         ms++)
      Poll(hw, 1);
    hw.led.Set(0.f, 0.f, 0.f);
    Poll(hw, 20);  // the debouncer reports a release a few polls later
    return both;
  }

  static void Poll(Carciofo& hw, int ms) {
    for (int i = 0; i < ms; i++) {
      hw.ProcessControls();
      daisy::System::Delay(1);
    }
  }

  bool Erase(int sector) {
    erased_[sector] = qspi_->EraseSector(base_ + sector * kSector) ==
                      daisy::QSPIHandle::Result::OK;
    return erased_[sector];
  }

  bool Append(const T& d) {
    if (offset_ + kRecord > kSector) {
      // On to the next sector, erased at power up unless a long session has
      // come all the way round.
      int next = (current_ + 1) % kSectors;
      if (!erased_[next] && !Erase(next)) return false;
      current_ = next;
      offset_ = 0;
    }
    erased_[current_] = false;

    Header h = {kMagic, version_, static_cast<uint16_t>(sizeof(T)),
                ++sequence_, 0};
    h.checksum = Checksum(h, d);
    uint8_t buf[kRecord];
    memset(buf, 0xff, sizeof(buf));
    memcpy(buf, &h, sizeof(h));
    memcpy(buf + sizeof(h), &d, sizeof(T));
    bool ok = qspi_->Write(base_ + current_ * kSector + offset_, kRecord,
                           buf) == daisy::QSPIHandle::Result::OK;
    // A failed write may have left part of a record behind, so the next save
    // goes past it either way.
    offset_ += kRecord;
    return ok;
  }

  daisy::QSPIHandle* qspi_ = nullptr;
  uint32_t base_ = 0;
  uint16_t version_ = 0;
  uint32_t sequence_ = 0;
  int current_ = 0;
  uint32_t offset_ = 0;
  bool erased_[kSectors] = {};
  uint32_t changed_ = 0;
  T data_, pending_, saved_;
};

}  // namespace carciofo
