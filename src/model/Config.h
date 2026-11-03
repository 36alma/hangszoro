#pragma once
#include <stdint.h>
#include <string>
#include <vector>

struct Config {
  std::string deviceName;
  std::string hostname;

  std::string wifiSsid;
  std::string wifiPassword;

  uint32_t ip = 0;
  uint32_t mask = 0;
  uint32_t gateway = 0;
  uint32_t dns = 0;

  std::string host;
  uint16_t port = 0;
  std::string path;
  std::string token;

  int bck = 0;
  int lrck = 0;
  int din = 0;

  int oledSda = 0;
  int oledScl = 0;

  uint32_t bufferMs = 0;
  uint32_t prebufferMs = 0;
};

// A config_defaults.h alapértékeiből épített konfiguráció.
Config defaultConfig();

// A JSON érvényes mezőit ráírja `cfg`-re. Az érvénytelen mezőket kihagyja,
// és mindegyikről figyelmeztetést ad vissza. A `net`, az `i2s` és a `buffer`
// csoport egyben érvényes vagy egyben elutasított. A token értékét nem írja a figyelmeztetésekbe.
std::vector<std::string> applyJson(Config& cfg, const char* json);

// Igaz, ha az OLED tűk 0..48 között vannak, különbözők, és egyik sem I2S tű.
bool oledPinsValid(const Config& cfg);
