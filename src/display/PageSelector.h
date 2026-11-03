#pragma once
#include <stdint.h>

// A BOOT gomb pergésmentesítése és a kijelző oldalainak léptetése. Hardverfüggetlen, az időt a hívó adja.
class PageSelector {
 public:
  static constexpr uint8_t kPages = 3;
  static constexpr uint32_t kDebounceMs = 30;

  // pressed: a gomb nyers állapota (pereghet). Igaz, ha ettől a hívástól oldal váltott.
  // Egy lenyomás akkor számít, ha a nyers állapot legalább kDebounceMs-ig változatlan; nyomva tartás nem ismétel.
  bool update(bool pressed, uint32_t nowMs);
  uint8_t page() const { return page_; }  // 0..kPages-1

 private:
  bool raw_ = false;
  bool stable_ = false;
  uint32_t rawSinceMs_ = 0;
  uint8_t page_ = 0;
};
