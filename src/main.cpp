#include <Arduino.h>
#include <LittleFS.h>

#include "AudioFileSourceBuffer.h"
#include "AudioFileSourceLittleFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"
#include "Log.h"
#include "config_defaults.h"

// Lánc: THIS.mp3 (LittleFS) -> puffer (darabokban töltve) -> MP3 dekóder -> PCM -> I2S -> PCM5102
constexpr const char* kMp3Path = "/THIS.mp3";
constexpr uint32_t kBufferBytes = 16 * 1024;  // a fájl és a dekóder közti puffer
constexpr float kGain = 0.5f;                 // 0.0 .. 1.0 (a DAC ne vágjon)

AudioFileSourceLittleFS* file = nullptr;
AudioFileSourceBuffer* buffer = nullptr;
AudioGeneratorMP3* mp3 = nullptr;
AudioOutputI2S* out = nullptr;

void startPlayback() {
  if (mp3) mp3->stop();
  delete mp3;
  delete buffer;
  delete file;
  file = new AudioFileSourceLittleFS(kMp3Path);
  buffer = new AudioFileSourceBuffer(file, kBufferBytes);
  mp3 = new AudioGeneratorMP3();
  if (!mp3->begin(buffer, out)) LOGLN("[mp3] begin failed");
}

void setup() {
  LOG_BEGIN(115200);
  delay(300);
  LOGLN("[boot] hangszoro");

  if (!LittleFS.begin(false)) {
    LOGLN("[fs] LittleFS mount failed (uploadfs kell)");
    return;
  }
  if (!LittleFS.exists(kMp3Path)) {
    LOGF("[fs] %s not found (pio run -t uploadfs)\n", kMp3Path);
    return;
  }

  out = new AudioOutputI2S();
  out->SetPinout(CFG_I2S_BCK, CFG_I2S_LRCK, CFG_I2S_DIN);
  out->SetGain(kGain);
  startPlayback();
}

void loop() {
  if (!mp3) return;
  if (mp3->isRunning()) {
    if (!mp3->loop()) mp3->stop();
  } else {
    LOGLN("[mp3] done, restarting");
    delay(500);
    startPlayback();  // végtelenített lejátszás
  }
}
