#pragma once
#include <stdint.h>

// "a.b.c.d" -> host byte order szám. Hibánál false, és `out` nem változik.
inline bool parseIPv4(const char* s, uint32_t& out) {
  if (!s) return false;
  uint32_t result = 0;
  int octets = 0;
  const char* p = s;
  while (true) {
    if (*p < '0' || *p > '9') return false;
    uint32_t v = 0;
    int digits = 0;
    while (*p >= '0' && *p <= '9') {
      v = v * 10 + (uint32_t)(*p - '0');
      if (++digits > 3 || v > 255) return false;
      p++;
    }
    result = (result << 8) | v;
    if (++octets == 4) break;
    if (*p != '.') return false;
    p++;
  }
  if (*p != '\0') return false;
  out = result;
  return true;
}

// Összefüggő 1-esek balról, legalább egy.
inline bool isValidMask(uint32_t mask) {
  if (mask == 0) return false;
  uint32_t inv = ~mask;
  return (inv & (inv + 1)) == 0;
}

inline bool sameSubnet(uint32_t a, uint32_t b, uint32_t mask) {
  return (a & mask) == (b & mask);
}
