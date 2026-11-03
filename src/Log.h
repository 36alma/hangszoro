#pragma once
// Soros napló a build módtól függően (platformio.ini: env:dev és env:prod).
// HANGSZORO_SERIAL_LOG=1 (dev): a napló a soros portra megy.
// HANGSZORO_SERIAL_LOG=0 (prod): a Serial el sem indul, a hívások kiesnek a fordításból.
// Az if (0) ág miatt prodban is ellenőrzi a fordító az argumentumokat, de kódot nem generál.

#include <Arduino.h>

#ifndef HANGSZORO_SERIAL_LOG
#define HANGSZORO_SERIAL_LOG 0
#endif

#if HANGSZORO_SERIAL_LOG
#define LOG_BEGIN(baud) Serial.begin(baud)
#define LOGF(...) Serial.printf(__VA_ARGS__)
#define LOGLN(msg) Serial.println(msg)
#else
#define LOG_BEGIN(baud) \
  do {                  \
  } while (0)
#define LOGF(...)                               \
  do {                                          \
    if (0) Serial.printf(__VA_ARGS__);          \
  } while (0)
#define LOGLN(msg)                              \
  do {                                          \
    if (0) Serial.println(msg);                 \
  } while (0)
#endif
