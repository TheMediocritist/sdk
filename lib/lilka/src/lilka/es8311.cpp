#include "es8311.h"

#include <Arduino.h>
#include <Wire.h>

#include "serial.h"

#ifndef LILKA_ES8311_ADDR
#    define LILKA_ES8311_ADDR 0x18
#endif

namespace lilka {

bool ES8311::writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(LILKA_ES8311_ADDR);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

int ES8311::readReg(uint8_t reg) {
    Wire.beginTransmission(LILKA_ES8311_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return -1;
    if (Wire.requestFrom(static_cast<int>(LILKA_ES8311_ADDR), 1) != 1) return -1;
    return Wire.read();
}

bool ES8311::begin() {
    // Probe
    Wire.beginTransmission(LILKA_ES8311_ADDR);
    if (Wire.endTransmission() != 0) {
        serial.err("ES8311 not found on I2C (addr 0x%02X)", LILKA_ES8311_ADDR);
        return false;
    }

    bool ok = true;
    // --- open() sequence (esp_codec_dev es8311.c), playback-relevant parts ---
    ok &= writeReg(0x44, 0x08); // GPIO: I2C noise immunity (written twice upstream)
    ok &= writeReg(0x44, 0x08);
    ok &= writeReg(0x01, 0x30); // CLK manager: enable clocks
    ok &= writeReg(0x02, 0x00); // pre_div=1, mult=1  (ratio-256 default)
    ok &= writeReg(0x03, 0x10); // ADC osr
    ok &= writeReg(0x16, 0x24); // ADC config
    ok &= writeReg(0x04, 0x10); // DAC osr
    ok &= writeReg(0x05, 0x00); // adc_div=1, dac_div=1
    ok &= writeReg(0x0B, 0x00); // system
    ok &= writeReg(0x0C, 0x00); // system
    ok &= writeReg(0x10, 0x1F); // system
    ok &= writeReg(0x11, 0x7F); // system
    ok &= writeReg(0x00, 0x80); // reset: power on, slave mode (bit6=0)
    ok &= writeReg(0x01, 0x3F); // MCLK from MCLK pin (bit7=0), not inverted, all clocks on
    // SCLK not inverted: clear bit5 of REG06
    int r6 = readReg(0x06);
    if (r6 >= 0) ok &= writeReg(0x06, r6 & ~0x20);
    ok &= writeReg(0x13, 0x10); // system: HP path
    ok &= writeReg(0x1B, 0x0A); // ADC
    ok &= writeReg(0x1C, 0x6A); // ADC
    ok &= writeReg(0x44, 0x08); // no DAC reference to ADC (no_dac_ref)

    // --- format: 16-bit I2S, DAC SDP on, ADC SDP muted ---
    ok &= writeReg(0x09, 0x0C); // SDP-IN (DAC): I2S, 16-bit, unmuted (bit6=0)
    ok &= writeReg(0x0A, 0x4C); // SDP-OUT (ADC): I2S, 16-bit, muted (bit6=1)

    // --- start() sequence, DAC path ---
    ok &= writeReg(0x17, 0xBF); // ADC volume (unused)
    ok &= writeReg(0x0E, 0x02); // analog power
    ok &= writeReg(0x12, 0x00); // DAC power on
    ok &= writeReg(0x14, 0x1A); // system: analog input config (mic path, unused)
    ok &= writeReg(0x0D, 0x01); // power up analog circuits
    ok &= writeReg(0x15, 0x40); // ADC softramp (unused)
    ok &= writeReg(0x37, 0x08); // DAC: ramp/EQ bypass config
    ok &= writeReg(0x45, 0x00); // GP control

    setVolume(80);

    if (ok) {
        serial.log("ES8311 initialized (slave, MCLK/256fs, 16-bit I2S)");
    } else {
        serial.err("ES8311 init: some register writes failed");
    }
    return ok;
}

void ES8311::setVolume(uint8_t percent) {
    if (percent > 100) percent = 100;
    // REG32: 0x00 = mute, 0xBF = 0 dB. Stay at or below 0 dB to avoid
    // digital clipping; software gain handles the rest.
    writeReg(0x32, (static_cast<uint16_t>(percent) * 0xBF) / 100);
}

void ES8311::setMute(bool mute) {
    int r = readReg(0x31);
    if (r < 0) return;
    writeReg(0x31, mute ? (r | 0x60) : (r & ~0x60));
}

ES8311 es8311;

} // namespace lilka
