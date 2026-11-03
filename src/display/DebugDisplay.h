#pragma once
// OLED debug kijelző (1.3" SH1106, I2C) a dev buildhez.
// HANGSZORO_OLED=0 (prod): üres inline stubok, a src/display/ fájlok be sem fordulnak
// (platformio.ini: env:prod build_src_filter), és az U8g2 sem kerül a buildbe.

#include <stddef.h>
#include <stdint.h>

#include <atomic>

#include "../model/Config.h"

#ifndef HANGSZORO_OLED
#define HANGSZORO_OLED 0
#endif

class StreamClient;
class RingBuffer;

namespace debugdisplay {

#if HANGSZORO_OLED
// Ellenőrzi a tűket, megkeresi a kijelzőt a 0x3C címen, és ha megvan, elindítja a "display" taskot.
// Hiányzó kijelzőnél vagy rossz tűknél egyszer naplóz, és false-t ad; minden más ugyanúgy megy.
bool begin(const Config& cfg, StreamClient& client, const RingBuffer& ring,
           const std::atomic<uint32_t>& beginFailures);
// A dekóder task hívja; ha a kijelző nem indult el, azonnal visszatér.
void onPcm(const int16_t* pcm, size_t frames, uint8_t channels, uint32_t sampleRate);
#else
inline bool begin(const Config&, StreamClient&, const RingBuffer&, const std::atomic<uint32_t>&) {
  return false;
}
inline void onPcm(const int16_t*, size_t, uint8_t, uint32_t) {}
#endif

}  // namespace debugdisplay
