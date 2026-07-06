#include "st7305.h"

#include <Arduino.h>

#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <esp_check.h>

namespace lilka {

ST7305::ST7305(int mosi, int sck, int dc, int cs, int rst, int width, int height, spi_host_device_t host) :
    mosi_(mosi), sck_(sck), dc_(dc), cs_(cs), rst_(rst), width_(width), height_(height), host_(host) {
}

void ST7305::begin() {
    packedLen_ = width_ * height_ / 8;
    packed_ = static_cast<uint8_t*>(heap_caps_malloc(packedLen_, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    assert(packed_);
    memset(packed_, 0xFF, packedLen_);

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = mosi_;
    buscfg.sclk_io_num = sck_;
    buscfg.miso_io_num = -1;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = packedLen_ + 64;
    ESP_ERROR_CHECK_WITHOUT_ABORT(spi_bus_initialize(host_, &buscfg, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t iocfg = {};
    iocfg.dc_gpio_num = dc_;
    iocfg.cs_gpio_num = cs_;
    iocfg.pclk_hz = 80 * 1000 * 1000;
    iocfg.lcd_cmd_bits = 8;
    iocfg.lcd_param_bits = 8;
    iocfg.spi_mode = 0;
    iocfg.trans_queue_depth = 10;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_lcd_new_panel_io_spi(reinterpret_cast<esp_lcd_spi_bus_handle_t>(host_), &iocfg, &io_));
    if (io_ == nullptr) {
        return;
    }

    gpio_config_t rstcfg = {};
    rstcfg.mode = GPIO_MODE_OUTPUT;
    rstcfg.pin_bit_mask = 1ULL << rst_;
    rstcfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gpio_config(&rstcfg);

    reset();

    // Init sequence from Waveshare BSP (verbatim register values).
    cmd(0xD6); // NVM Load Control
    data(0x17);
    data(0x02);
    cmd(0xD1); // Booster Enable
    data(0x01);
    cmd(0xC0); // Gate Voltage Control
    data(0x11);
    data(0x04);
    cmd(0xC1); // VSHP
    data(0x69);
    data(0x69);
    data(0x69);
    data(0x69);
    cmd(0xC2); // VSLP
    data(0x19);
    data(0x19);
    data(0x19);
    data(0x19);
    cmd(0xC4); // VSHN
    data(0x4B);
    data(0x4B);
    data(0x4B);
    data(0x4B);
    cmd(0xC5); // VSLN
    data(0x19);
    data(0x19);
    data(0x19);
    data(0x19);
    cmd(0xD8); // OSC
    data(0x80);
    data(0xE9);
    cmd(0xB2); // Frame Rate Control
    data(0x02);
    cmd(0xB3); // Update Period Gate EQ Control (HPM)
    data(0xE5);
    data(0xF6);
    data(0x05);
    data(0x46);
    data(0x77);
    data(0x77);
    data(0x77);
    data(0x77);
    data(0x76);
    data(0x45);
    cmd(0xB4); // Update Period Gate EQ Control (LPM)
    data(0x05);
    data(0x46);
    data(0x77);
    data(0x77);
    data(0x77);
    data(0x77);
    data(0x76);
    data(0x45);
    cmd(0x62); // Gate Timing Control
    data(0x32);
    data(0x03);
    data(0x1F);
    cmd(0xB7); // Source EQ Enable
    data(0x13);
    cmd(0xB0); // Gate Line Setting: 0x64 * 4 = 400 lines
    data(0x64);
    cmd(0x11); // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(200));
    cmd(0xC9); // Source Voltage Select
    data(0x00);
    cmd(0x36); // Memory Data Access Control
    data(0x48);
    cmd(0x3A); // Data Format: 1bpp
    data(0x11);
    cmd(0xB9); // Gamma Mode: mono
    data(0x20);
    cmd(0xB8); // Panel Setting
    data(0x29);
    cmd(0x21); // Display Inversion On
    setWindow();
    cmd(0x35); // Tearing Effect On (TE pin = GPIO6, unused for now)
    data(0x00);
    cmd(0xD0); // Auto power down
    data(0xFF);
    cmd(0x38); // High Power Mode On
    cmd(0x29); // Display On

    clear(true);
}

void ST7305::reset() {
    gpio_set_level(static_cast<gpio_num_t>(rst_), 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(static_cast<gpio_num_t>(rst_), 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(static_cast<gpio_num_t>(rst_), 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void ST7305::cmd(uint8_t reg) {
    esp_lcd_panel_io_tx_param(io_, reg, nullptr, 0);
}

void ST7305::data(uint8_t d) {
    esp_lcd_panel_io_tx_param(io_, -1, &d, 1);
}

void ST7305::setWindow() {
    cmd(0x2A); // Column Address Set
    data(0x12);
    data(0x2A);
    cmd(0x2B); // Page Address Set
    data(0x00);
    data(0xC7);
}

void ST7305::sendPacked() {
    setWindow();
    cmd(0x2C); // Memory Write
    esp_lcd_panel_io_tx_color(io_, -1, packed_, packedLen_);
}

void ST7305::clear(bool white) {
    memset(packed_, white ? 0xFF : 0x00, packedLen_);
    sendPacked();
}

// Repack row-major MSB-first 1bpp (Arduino_Canvas_Mono) into ST7305 landscape
// block format. Panel memory layout (from Waveshare's LUT, inverted):
//   output byte (bx, by), bx in [0, W/2), by in [0, H/4)
//   holds a 2-wide x 4-tall pixel block at x = 2*bx, inv_y = 4*by
//   with y flipped: y = H-1-inv_y
//   bit position = 7 - (local_y*2 + local_x)
// Since x = 2*bx is always even, both horizontal pixels of a block row live
// in the same source byte, so we gather bit *pairs* — 4 reads per output byte.
void ST7305::repack(const uint8_t* src) {
    const int stride = (width_ + 7) / 8; // 50 bytes for 400 px
    const int hBlocks = height_ / 4; // 75
    uint8_t* out = packed_;
    for (int bx = 0; bx < width_ / 2; bx++) {
        const int x = bx * 2;
        const int srcByte = x >> 3;
        const int shift = 6 - (x & 6); // pair position within source byte (6,4,2,0)
        for (int by = 0; by < hBlocks; by++) {
            // local_y = 0..3 maps to inv_y = 4*by + local_y, y = H-1-inv_y.
            // Consecutive local_y means DEcreasing y: y0 = H-1-4*by, then y0-1, ...
            const int y0 = height_ - 1 - by * 4;
            uint8_t b = 0;
            b |= ((src[y0 * stride + srcByte] >> shift) & 0x3) << 6; // local_y=0 -> bits 7..6
            b |= ((src[(y0 - 1) * stride + srcByte] >> shift) & 0x3) << 4;
            b |= ((src[(y0 - 2) * stride + srcByte] >> shift) & 0x3) << 2;
            b |= (src[(y0 - 3) * stride + srcByte] >> shift) & 0x3;
            *out++ = b;
        }
    }
}

void ST7305::pushFrame(const uint8_t* src) {
    repack(src);
    if (teSync_) {
        waitTE();
    }
    sendPacked();
}

void ST7305::enableTESync(int tePin) {
    tePin_ = tePin;
    gpio_config_t tecfg = {};
    tecfg.mode = GPIO_MODE_INPUT;
    tecfg.pin_bit_mask = 1ULL << tePin_;
    gpio_config(&tecfg);
    teSync_ = true;
}

// Wait for the next TE rising edge (start of vertical blanking), so the RAM
// write races ahead of the panel scan instead of shearing through it.
void ST7305::waitTE() {
    const uint32_t timeoutUs = 100000; // don't hang if TE is unwired/misconfigured
    uint32_t start = micros();
    // If we're inside a pulse, wait for it to end first.
    while (gpio_get_level(static_cast<gpio_num_t>(tePin_)) == 1) {
        if (micros() - start > timeoutUs) return;
    }
    while (gpio_get_level(static_cast<gpio_num_t>(tePin_)) == 0) {
        if (micros() - start > timeoutUs) return;
    }
}

} // namespace lilka
