// Minimal ES8311 codec driver (playback path only) for the Waveshare
// ESP32-S3-RLCD-4.2. Distilled from espressif's esp_codec_dev es8311.c
// (vendored in Waveshare's repo): slave mode, MCLK from the MCLK pin at
// 256*fs (the chip's power-on divider defaults ARE the ratio-256 config,
// so one init tracks any sample rate as long as MCLK scales with fs -
// which the ESP32 I2S driver does automatically), 16-bit I2S.
// The ES7210 mic ADC is not touched.
#ifndef LILKA_ES8311_H
#define LILKA_ES8311_H

#include <stdint.h>

#include "config.h"

namespace lilka {

class ES8311 {
public:
    // Initialize the codec over I2C (Wire must already be begun) and power
    // up the DAC path. Returns false if the chip doesn't ACK.
    bool begin();

    // Hardware DAC volume, 0..100 (100 = 0 dB full scale).
    void setVolume(uint8_t percent);

    void setMute(bool mute);

private:
    bool writeReg(uint8_t reg, uint8_t value);
    int readReg(uint8_t reg);
};

extern ES8311 es8311;

} // namespace lilka

#endif // LILKA_ES8311_H
