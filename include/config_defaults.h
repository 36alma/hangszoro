#pragma once
// Alapértékek. A data/config.json felülírja őket.

#define CFG_DEVICE_NAME "Hangszoro"
#define CFG_DEVICE_HOSTNAME "hangszoro"

#define CFG_WIFI_SSID "changeme"
#define CFG_WIFI_PASSWORD "changeme"

#define CFG_NET_IP "192.168.1.50"
#define CFG_NET_MASK "255.255.255.0"
#define CFG_NET_GATEWAY "192.168.1.1"
#define CFG_NET_DNS "192.168.1.1"

#define CFG_ENDPOINT_HOST "192.168.1.10"
#define CFG_ENDPOINT_PORT 8766
#define CFG_ENDPOINT_PATH "/api/mixer/stream"
#define CFG_ENDPOINT_TOKEN ""

#define CFG_I2S_BCK 15
#define CFG_I2S_LRCK 16
#define CFG_I2S_DIN 17

// OLED debug kijelző (SH1106, I2C), csak a dev buildben használt.
#define CFG_OLED_SDA 8
#define CFG_OLED_SCL 18

#define CFG_BUFFER_MS 500
#define CFG_PREBUFFER_MS 150
