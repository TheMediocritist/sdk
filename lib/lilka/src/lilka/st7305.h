// ST7305 reflective LCD driver (Waveshare ESP32-S3-RLCD-4.2, 400x300 1bpp).
// Validated standalone before integration: init sequence from Waveshare's
// BSP, LUT-free repack (bit-exact vs their algorithm), TE-synced push,
// stable at 80 MHz SPI (hardware max; well above the panel's spec, tested OK).
#ifndef LILKA_ST7305_H
#define LILKA_ST7305_H

#include <stdint.h>
#include <driver/spi_master.h>
#include <esp_lcd_panel_io.h>

#include "config.h"

namespace lilka {

class ST7305 {
public:
    ST7305(
        int mosi = LILKA_DISPLAY_MOSI, int sck = LILKA_DISPLAY_SCK, int dc = LILKA_DISPLAY_DC,
        int cs = LILKA_DISPLAY_CS, int rst = LILKA_DISPLAY_RST, int width = LILKA_DISPLAY_WIDTH,
        int height = LILKA_DISPLAY_HEIGHT, spi_host_device_t host = SPI3_HOST
    );

    void begin();

    // src: row-major 1bpp, stride = (width+7)/8, MSB-first, bit set = white.
    // This is Arduino_Canvas_Mono's horizontal framebuffer layout.
    void pushFrame(const uint8_t* src);

    void clear(bool white);

    // Sync pushFrame to the panel's tearing-effect pulse.
    void enableTESync(int tePin = LILKA_DISPLAY_TE);

    int width() const {
        return width_;
    }
    int height() const {
        return height_;
    }

private:
    void reset();
    void cmd(uint8_t reg);
    void data(uint8_t d);
    void sendPacked();
    void setWindow();
    void repack(const uint8_t* src);
    void waitTE();

    esp_lcd_panel_io_handle_t io_ = nullptr;
    int mosi_, sck_, dc_, cs_, rst_;
    int width_, height_;
    spi_host_device_t host_;
    uint8_t* packed_ = nullptr;
    int packedLen_ = 0;
    bool teSync_ = false;
    int tePin_ = -1;
};

} // namespace lilka

#endif // LILKA_ST7305_H
