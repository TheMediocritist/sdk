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

    /// Push a subregion of the framebuffer only. Coordinates are in the
    /// panel's 2x4-block grid (x, w are in blocks of 2 pixels; y, h in
    /// blocks of 4 pixels). Caller is responsible for rounding pixel
    /// rectangles outward - see ST7305::alignRectToBlocks().
    /// Uses a small stack buffer + windowed write. Assumes TE sync setup
    /// same as pushFrame.
    void pushPartial(const uint8_t* src, int bx, int by, int bw, int bh);

    /// Round pixel rect (x, y, w, h) outward to the 2x4 block grid, in place.
    /// After call, x/y are top-left pixel of the containing block, w/h are
    /// pixel dimensions covering all touched blocks. Returns block-space
    /// coords in bx/by/bw/bh (each pixel-w/2, pixel-h/4).
    void alignRectToBlocks(
        int16_t& x, int16_t& y, int16_t& w, int16_t& h, int& bx, int& by, int& bw, int& bh
    ) const;

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
    void setWindowBlocks(int bx, int by, int bw, int bh);
    void repack(const uint8_t* src);
    void repackPartial(const uint8_t* src, int bx, int by, int bw, int bh, uint8_t* dst);
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
