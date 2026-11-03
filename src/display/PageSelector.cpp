#include "PageSelector.h"

bool PageSelector::update(bool pressed, uint32_t nowMs) {
  if (pressed != raw_) {
    raw_ = pressed;
    rawSinceMs_ = nowMs;
  }
  // Előjel nélküli kivonás: a millis() túlcsordulásán is helyes.
  if (raw_ == stable_ || (uint32_t)(nowMs - rawSinceMs_) < kDebounceMs) return false;
  stable_ = raw_;
  if (!stable_) return false;
  page_ = (uint8_t)((page_ + 1) % kPages);
  return true;
}
