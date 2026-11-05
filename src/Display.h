#pragma once
// OLED debug kijelző (1.3" SH1106, 128x64, I2C) a dev buildhez.
// HANGSZORO_OLED=0 (prod): üres stub, a Display.cpp és az U8g2 nem fordul be.

#include <Arduino.h>

#ifndef HANGSZORO_OLED
#define HANGSZORO_OLED 0
#endif

#if HANGSZORO_OLED
// Megkeresi a kijelzőt (0x3C), és ha megvan, elindítja a "display" taskot a 0-s magon.
// Ha nincs kijelző, egyszer naplóz, és minden más ugyanúgy megy.
bool displayBegin(int sda, int scl, const String& deviceName, const String& ip);
#else
inline bool displayBegin(int, int, const String&, const String&) { return false; }
#endif
