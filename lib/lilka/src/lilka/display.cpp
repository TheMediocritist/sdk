#include "display.h"

#include "default_splash.h"
#include "spi.h"
#include "serial.h"
#include "fmath.h"
#include "buzzer.h"

namespace lilka {

template class GFX<Display>;
template class GFX<Canvas>;

static ST7305 rlcdPanel; // pins/dimensions from config.h

// ---------- MonoCanvas ----------

MonoCanvas::MonoCanvas(int16_t w, int16_t h, int16_t x, int16_t y) : Arduino_Canvas_Mono(w, h, nullptr, x, y) {
}

void MonoCanvas::writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    // Той самий поріг біле/чорне, що і в Arduino_Canvas_Mono::writePixelPreclipped.
    const bool white = color & 0b1000010000010000;
    uint8_t* fb = getFramebuffer();
    const int16_t stride = monoStride();

    if (x == 0 && w == width()) {
        memset(fb + y * stride, white ? 0xFF : 0x00, static_cast<size_t>(h) * stride);
        return;
    }

    const int16_t x2 = x + w - 1;
    const int16_t firstByte = x >> 3;
    const int16_t lastByte = x2 >> 3;
    const uint8_t headMask = 0xFF >> (x & 7);
    const uint8_t tailMask = 0xFF << (7 - (x2 & 7));

    for (int16_t row = y; row < y + h; row++) {
        uint8_t* line = fb + row * stride;
        if (firstByte == lastByte) {
            const uint8_t m = headMask & tailMask;
            if (white) {
                line[firstByte] |= m;
            } else {
                line[firstByte] &= ~m;
            }
        } else {
            if (white) {
                line[firstByte] |= headMask;
                if (lastByte - firstByte > 1) memset(line + firstByte + 1, 0xFF, lastByte - firstByte - 1);
                line[lastByte] |= tailMask;
            } else {
                line[firstByte] &= ~headMask;
                if (lastByte - firstByte > 1) memset(line + firstByte + 1, 0x00, lastByte - firstByte - 1);
                line[lastByte] &= ~tailMask;
            }
        }
    }
}

void MonoCanvas::writeFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
    if (w < 1 || y < 0 || y >= height()) return;
    if (x < 0) {
        w += x;
        x = 0;
    }
    if (x + w > width()) w = width() - x;
    if (w < 1) return;
    writeFillRectPreclipped(x, y, w, 1, color);
}

void MonoCanvas::blitMono(MonoCanvas* src, int16_t destX, int16_t destY, int16_t w, int16_t h) {
    if (w < 0) w = src->width();
    if (h < 0) h = src->height();
    if (w > src->width()) w = src->width();
    if (h > src->height()) h = src->height();

    // Швидкий шлях: повна ширина, вирівняно по байтах - memcpy рядків.
    if (destX == 0 && w == width() && src->width() == width()) {
        const int16_t stride = monoStride();
        int16_t rows = h;
        if (destY + rows > height()) rows = height() - destY;
        if (rows <= 0 || destY < 0) return;
        memcpy(getFramebuffer() + destY * stride, src->getFramebuffer(), static_cast<size_t>(rows) * stride);
        return;
    }

    // Загальний шлях: попіксельно.
    const uint8_t* sfb = src->getFramebuffer();
    const int16_t sstride = src->monoStride();
    for (int16_t sy = 0; sy < h; sy++) {
        const int16_t dy = destY + sy;
        if (dy < 0 || dy >= height()) continue;
        const uint8_t* line = sfb + sy * sstride;
        for (int16_t sx = 0; sx < w; sx++) {
            const int16_t dx = destX + sx;
            if (dx < 0 || dx >= width()) continue;
            const bool white = line[sx >> 3] & (0x80 >> (sx & 7));
            writePixelPreclipped(dx, dy, white ? 0xFFFF : 0x0000);
        }
    }
}

// ---------- Display ----------

Display::Display() : MonoCanvas(LILKA_DISPLAY_WIDTH, LILKA_DISPLAY_HEIGHT), splash(NULL), rleLength(0) {
}

void Display::begin() {
    serial.log("initializing display (ST7305 mono)");
    presentMutex = xSemaphoreCreateMutex();
    rlcdPanel.begin();
    rlcdPanel.enableTESync(LILKA_DISPLAY_TE);
    Arduino_Canvas_Mono::begin(GFX_SKIP_OUTPUT_BEGIN);
    fillScreen(lilka::colors::Black);
    setFont(FONT_10x20);
    setUTF8Print(true);
    if (autoPresentMs > 0) {
        xTaskCreatePinnedToCore(presentTask, "present", 4096, this, 1, &presentTaskHandle, 0);
    }
    serial.log("display ok");
}

void Display::present() {
    if (presentMutex == NULL) return;
    xSemaphoreTake(presentMutex, portMAX_DELAY);
    if (dirtyMode == DirtyMode::Full || dirtyW == width()) {
        // Full push covers the "everything dirty" case at the same cost as
        // a partial push of the same area, but skips the alignment math.
        rlcdPanel.pushFrame(getFramebuffer());
    } else if (dirtyW > 0 && dirtyH > 0) {
        int16_t x = dirtyX, y = dirtyY, w = dirtyW, h = dirtyH;
        int bx, by, bw, bh;
        rlcdPanel.alignRectToBlocks(x, y, w, h, bx, by, bw, bh);
        rlcdPanel.pushPartial(getFramebuffer(), bx, by, bw, bh);
    }
    // Tracked/Auto with no dirty rect: silent no-op (this is the win).
    dirtyX = dirtyY = dirtyW = dirtyH = 0;
    xSemaphoreGive(presentMutex);
}

void Display::markDirty(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (dirtyMode == DirtyMode::Full) return;
    // Clip to screen.
    int16_t x2 = x + w, y2 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > width()) x2 = width();
    if (y2 > height()) y2 = height();
    if (x2 <= x || y2 <= y) return;
    if (dirtyW == 0) {
        dirtyX = x;
        dirtyY = y;
        dirtyW = x2 - x;
        dirtyH = y2 - y;
    } else {
        // Union with existing rect.
        int16_t ux = dirtyX < x ? dirtyX : x;
        int16_t uy = dirtyY < y ? dirtyY : y;
        int16_t ux2 = (dirtyX + dirtyW) > x2 ? (dirtyX + dirtyW) : x2;
        int16_t uy2 = (dirtyY + dirtyH) > y2 ? (dirtyY + dirtyH) : y2;
        dirtyX = ux;
        dirtyY = uy;
        dirtyW = ux2 - ux;
        dirtyH = uy2 - uy;
    }
}

bool Display::getDirtyRect(int16_t* x, int16_t* y, int16_t* w, int16_t* h) const {
    if (dirtyW == 0) return false;
    if (x) *x = dirtyX;
    if (y) *y = dirtyY;
    if (w) *w = dirtyW;
    if (h) *h = dirtyH;
    return true;
}

void Display::clearDirty() {
    dirtyX = dirtyY = dirtyW = dirtyH = 0;
}

void Display::writeFillRectPreclipped(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    MonoCanvas::writeFillRectPreclipped(x, y, w, h, color);
    if (dirtyMode == DirtyMode::Auto) {
        markDirty(x, y, w, h);
    }
}

void Display::writePixelPreclipped(int16_t x, int16_t y, uint16_t color) {
    MonoCanvas::writePixelPreclipped(x, y, color);
    if (dirtyMode == DirtyMode::Auto) {
        markDirty(x, y, 1, 1);
    }
}

void Display::fillScreenTracked(uint16_t color) {
    if (dirtyMode == DirtyMode::Full) {
        fillScreen(color);
    }
    // In Tracked/Auto: no-op. Caller is opting out of "clear every frame";
    // they'll draw over their dirty regions themselves.
    (void)color;
}

void Display::presentTask(void* arg) {
    Display* self = static_cast<Display*>(arg);
    while (true) {
        uint32_t ms = self->autoPresentMs;
        if (ms == 0) ms = 1000;
        vTaskDelay(ms / portTICK_PERIOD_MS);
        if (self->autoPresentMs > 0) {
            self->present();
        }
    }
}

void Display::setAutoPresent(uint32_t intervalMs) {
    autoPresentMs = intervalMs;
}

void Display::drawCanvas(Canvas* canvas) {
    blitMono(canvas, canvas->x(), canvas->y());
    if (dirtyMode != DirtyMode::Full) {
        markDirty(canvas->x(), canvas->y(), canvas->width(), canvas->height());
    }
    present();
}

void Display::drawCanvasInterlaced(Canvas* canvas, bool odd) {
    (void)odd; // ST7789 tearing workaround - not applicable to ST7305
    drawCanvas(canvas);
}

void Display::showStartupScreen() {
    fillScreen(lilka::colors::White);
    setTextColor(lilka::colors::Black);
    drawTextAligned("KEIRA / RLCD", width() / 2, height() / 2, ALIGN_CENTER, ALIGN_CENTER);
    present();
}

void Display::setSplash(const void* splash, uint32_t rleLength) {
    // RGB565 splash images are not supported on the mono display.
    this->splash = splash;
    this->rleLength = rleLength;
}

uint16_t Display::color565hsv(uint16_t h, uint8_t s, uint8_t v) {
    uint8_t region, remainder, p, q, t;
    uint16_t red, green, blue;

    if (s == 0) {
        red = green = blue = (v * 31) / 100;
        return (red << 11) | (green << 5) | blue;
    }

    region = h / 60;
    remainder = (h - (region * 60)) * 6;

    p = (v * (100 - s)) / 100;
    q = (v * (100 - (s * remainder) / 100)) / 100;
    t = (v * (100 - (s * (60 - remainder)) / 100)) / 100;

    switch (region) {
        case 0:
            red = v;
            green = t;
            blue = p;
            break;
        case 1:
            red = q;
            green = v;
            blue = p;
            break;
        case 2:
            red = p;
            green = v;
            blue = t;
            break;
        case 3:
            red = p;
            green = q;
            blue = v;
            break;
        case 4:
            red = t;
            green = p;
            blue = v;
            break;
        default:
            red = v;
            green = p;
            blue = q;
            break;
    }

    red = (red * 31) / 100;
    green = (green * 63) / 100;
    blue = (blue * 31) / 100;

    return (red << 11) | (green << 5) | blue;
}

template <typename T>
void GFX<T>::drawImage(Image* image, int16_t x, int16_t y) {
    Arduino_GFX* base = static_cast<T*>(this);
    if (image->transparentColor == -1) {
        base->draw16bitRGBBitmap(x - image->pivotX, y - image->pivotY, image->pixels, image->width, image->height);
    } else {
        base->draw16bitRGBBitmapWithTranColor(
            x - image->pivotX, y - image->pivotY, image->pixels, image->transparentColor, image->width, image->height
        );
    }
}

template <typename T>
void GFX<T>::drawImageTransformed(Image* image, int16_t destX, int16_t destY, Transform transform) {
    // Transform image around its pivot.
    // Draw the rotated image at the specified position.

    int32_t imageWidth = image->width;
    int32_t imageHeight = image->height;

    // Calculate the coordinates of the four corners of the destination rectangle.
    int_vector_t v1 = transform.transform(int_vector_t{-image->pivotX, -image->pivotY});
    int_vector_t v2 = transform.transform(int_vector_t{imageWidth - image->pivotX, -image->pivotY});
    int_vector_t v3 = transform.transform(int_vector_t{-image->pivotX, imageHeight - image->pivotY});
    int_vector_t v4 = transform.transform(int_vector_t{imageWidth - image->pivotX, imageHeight - image->pivotY});

    // Find the bounding box of the transformed image.
    int_vector_t topLeft = int_vector_t{min(min(v1.x, v2.x), min(v3.x, v4.x)), min(min(v1.y, v2.y), min(v3.y, v4.y))};
    int_vector_t bottomRight =
        int_vector_t{max(max(v1.x, v2.x), max(v3.x, v4.x)), max(max(v1.y, v2.y), max(v3.y, v4.y))};

    if (bottomRight.x - topLeft.x == 0 || bottomRight.y - topLeft.y == 0) {
        // The transformed image is empty.
        lilka::serial.err("Transform leads to image with zero width or height");
        return;
    }

    // Create a new image to hold the transformed image.
    Image destImage(bottomRight.x - topLeft.x, bottomRight.y - topLeft.y, image->transparentColor, 0, 0);

    // Draw the transformed image to the new image.
    Transform inverse = transform.inverse();
    int_vector_t point{0, 0};
    for (point.y = topLeft.y; point.y < bottomRight.y; point.y++) {
        for (point.x = topLeft.x; point.x < bottomRight.x; point.x++) {
            int_vector_t v = inverse.transform(point);
            // Apply pivot offset
            v.x += image->pivotX;
            v.y += image->pivotY;
            if (v.x >= 0 && v.x < image->width && v.y >= 0 && v.y < image->height) {
                destImage.pixels[point.x - topLeft.x + (point.y - topLeft.y) * destImage.width] =
                    image->pixels[v.x + v.y * image->width];
            } else {
                destImage.pixels[point.x - topLeft.x + (point.y - topLeft.y) * destImage.width] =
                    image->transparentColor;
            }
        }
    }

    drawImage(&destImage, destX + topLeft.x, destY + topLeft.y);
}

// Чомусь в Arduino_GFX немає варіанту цього методу для const uint16_t[] - є лише для uint16_t.
void Display::draw16bitRGBBitmapWithTranColor(
    int16_t x, int16_t y, const uint16_t bitmap[], uint16_t transparent_color, int16_t w, int16_t h
) {
    // Цей cast безпечний, оскільки Arduino_GFX.draw16bitRGBBitmapWithTranColor не змінює bitmap.
    Arduino_GFX::draw16bitRGBBitmapWithTranColor(x, y, const_cast<uint16_t*>(bitmap), transparent_color, w, h);
}

uint8_t* Display::getFont() {
    return u8g2Font;
}



void Canvas::draw16bitRGBBitmapWithTranColor(
    int16_t x, int16_t y, const uint16_t bitmap[], uint16_t transparent_color, int16_t w, int16_t h
) {
    // Цей cast безпечний, оскільки Arduino_GFX.draw16bitRGBBitmapWithTranColor не змінює bitmap.
    Arduino_GFX::draw16bitRGBBitmapWithTranColor(x, y, const_cast<uint16_t*>(bitmap), transparent_color, w, h);
}

template <typename T>
void GFX<T>::drawCanvas(Canvas* canvas) {
    static_cast<T*>(this)->blitMono(canvas, canvas->x(), canvas->y());
}

Canvas::Canvas() : MonoCanvas(display.width(), display.height()) {
    setFont(u8g2_font_10x20_t_cyrillic);
    setUTF8Print(true);
    begin();
}

Canvas::Canvas(uint16_t width, uint16_t height) : MonoCanvas(width, height) {
    setFont(u8g2_font_10x20_t_cyrillic);
    setUTF8Print(true);
    begin();
}

Canvas::Canvas(uint16_t x, uint16_t y, uint16_t width, uint16_t height) : MonoCanvas(width, height, x, y) {
    setFont(u8g2_font_10x20_t_cyrillic);
    setUTF8Print(true);
    begin();
}

template <typename T>
int GFX<T>::drawTextAligned(const char* text, int16_t x, int16_t y, Alignment hAlign, Alignment vAlign) {
    // TODO: WARNING: This will break if we're not using U8g2 fonts.
    int16_t _x1, _y1;
    uint16_t w, _h;
    // U8g2 is a can of worms.
    T* base = static_cast<T*>(this);
    const uint8_t* font = base->getFont();
    const int8_t ascent = font[13]; // >0 (above the baseline, character 'A')
    const int8_t descent = font[14]; // <0 (below the baseline, character 'g')
    base->getTextBounds(text, 0, 0, &_x1, &_y1, &w, &_h);
    switch (hAlign) {
        case Alignment::ALIGN_START:
            break;
        case Alignment::ALIGN_CENTER:
            x -= w / 2;
            break;
        case Alignment::ALIGN_END:
            x -= w;
            break;
    }
    switch (vAlign) {
        case Alignment::ALIGN_START:
            y += ascent;
            break;
        case Alignment::ALIGN_CENTER:
            y += (ascent - descent) / 2;
            break;
        case Alignment::ALIGN_END:
            y += descent;
            break;
    }
    base->setCursor(x, y);
    base->print(text);
    return w;
}

template <typename T>
void GFX<T>::getTextBoundsAligned(
    const char* text, int16_t x, int16_t y, Alignment hAlign, Alignment vAlign, int16_t* x1, int16_t* y1, uint16_t* w,
    uint16_t* h
) {
    // TODO: WARNING: This will break if we're not using U8g2 fonts.
    // U8g2 is a can of worms.
    T* base = static_cast<T*>(this);
    const uint8_t* font = base->getFont();
    const int8_t ascent = font[13]; // >0 (above the baseline, character 'A')
    const int8_t descent = font[14]; // <0 (below the baseline, character 'g')
    base->getTextBounds(text, x, y, x1, y1, w, h);
    switch (hAlign) {
        case Alignment::ALIGN_START:
            break;
        case Alignment::ALIGN_CENTER:
            *x1 -= *w / 2;
            break;
        case Alignment::ALIGN_END:
            *x1 -= *w;
            break;
    }
    switch (vAlign) {
        case Alignment::ALIGN_START:
            *y1 += ascent;
            break;
        case Alignment::ALIGN_CENTER:
            *y1 += (ascent - descent) / 2;
            break;
        case Alignment::ALIGN_END:
            *y1 += descent;
            break;
    }
}

int16_t Canvas::x() {
    return _output_x;
}

int16_t Canvas::y() {
    return _output_y;
}

uint8_t* Canvas::getFont() {
    return u8g2Font;
}

int16_t getTextWidth(const uint8_t* font, const char* text) {
    lilka::Canvas canvas(0, 0);
    canvas.setFont(font);
    canvas.setTextWrap(false);
    int16_t x1, y1;
    uint16_t w, h;
    canvas.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return w;
}

Image::Image(uint32_t width, uint32_t height, int32_t transparentColor, int16_t pivotX, int16_t pivotY) :
    width(width), height(height), transparentColor(transparentColor), pivotX(pivotX), pivotY(pivotY) {
    // Allocate pixels in PSRAM
    pixels = static_cast<uint16_t*>(ps_malloc(width * height * sizeof(uint16_t)));
}

Image::~Image() {
    delete[] pixels;
}

Image* Image::newFromRLE(
    const uint8_t* data, uint32_t length, uint32_t width, uint32_t height, int32_t transparentColor, int16_t pivotX,
    int16_t pivotY
) {
    Image* image = new Image(width, height, transparentColor, width / 2, height / 2);
    RLEDecoder decoder(data, length);
    for (uint32_t i = 0; i < width * height; i++) {
        image->pixels[i] = decoder.next();
    }
    return image;
}

void Image::rotate(int16_t angle, Image* dest, int32_t blankColor) {
    // Rotate the image clockwise (Y-axis points down)
    int cx = width / 2;
    int cy = height / 2;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int dx = x - cx;
            int dy = y - cy;
            int x2 = cx + dx * fCos360(angle) + dy * fSin360(angle);
            int y2 = cy - dx * fSin360(angle) + dy * fCos360(angle);
            if (x2 >= 0 && x2 < width && y2 >= 0 && y2 < height) {
                dest->pixels[x + y * width] = pixels[x2 + y2 * width];
            } else {
                dest->pixels[x + y * width] = blankColor;
            }
        }
    }
}

void Image::flipX(Image* dest) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            dest->pixels[x + y * width] = pixels[(width - 1 - x) + y * width];
        }
    }
}

void Image::flipY(Image* dest) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            dest->pixels[x + y * width] = pixels[x + (height - 1 - y) * width];
        }
    }
}

Transform::Transform() : matrix{{1, 0}, {0, 1}} {
}

// Copy constructor
Transform::Transform(const Transform& other) {
    matrix[0][0] = other.matrix[0][0];
    matrix[0][1] = other.matrix[0][1];
    matrix[1][0] = other.matrix[1][0];
    matrix[1][1] = other.matrix[1][1];
}

// Copy assignment operator
Transform& Transform::operator=(const Transform& other) {
    if (this != &other) {
        matrix[0][0] = other.matrix[0][0];
        matrix[0][1] = other.matrix[0][1];
        matrix[1][0] = other.matrix[1][0];
        matrix[1][1] = other.matrix[1][1];
    }
    return *this;
}

Transform Transform::multiply(Transform other) {
    // Apply other transform to this transform
    Transform t;
    t.matrix[0][0] = matrix[0][0] * other.matrix[0][0] + matrix[0][1] * other.matrix[1][0];
    t.matrix[0][1] = matrix[0][0] * other.matrix[0][1] + matrix[0][1] * other.matrix[1][1];
    t.matrix[1][0] = matrix[1][0] * other.matrix[0][0] + matrix[1][1] * other.matrix[1][0];
    t.matrix[1][1] = matrix[1][0] * other.matrix[0][1] + matrix[1][1] * other.matrix[1][1];
    return t;
}

Transform Transform::rotate(int16_t angle) {
    // Rotate this transform by angle. Do this by multiplying with a rotation matrix.
    Transform t;
    t.matrix[0][0] = fCos360(angle);
    t.matrix[0][1] = -fSin360(angle);
    t.matrix[1][0] = fSin360(angle);
    t.matrix[1][1] = fCos360(angle);
    // Apply the rotation to this transform
    return t.multiply(*this);
}

Transform Transform::scale(float sx, float sy) {
    // Scale this transform by sx and sy. Do this by multiplying with a scaling matrix.
    if (sx == 0 || sy == 0) {
        // Scaling by zero is not allowed
        serial.err("Scaling by zero is not allowed, attempted to scale by %f, %f", sx, sy);
        return *this;
    }
    Transform t;
    t.matrix[0][0] = sx;
    t.matrix[0][1] = 0;
    t.matrix[1][0] = 0;
    t.matrix[1][1] = sy;
    // Apply the scaling to this transform
    return t.multiply(*this);
}

Transform Transform::inverse() {
    // Calculate the inverse of this transform
    Transform t;
    float det = matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0];
    t.matrix[0][0] = matrix[1][1] / det;
    t.matrix[0][1] = -matrix[0][1] / det;
    t.matrix[1][0] = -matrix[1][0] / det;
    t.matrix[1][1] = matrix[0][0] / det;
    return t;
}

inline int_vector_t Transform::transform(int_vector_t v) {
    // Apply this transform to a vector
    return int_vector_t{
        static_cast<int32_t>(matrix[0][0] * v.x + matrix[0][1] * v.y),
        static_cast<int32_t>(matrix[1][0] * v.x + matrix[1][1] * v.y),
    };
}

RLEDecoder::RLEDecoder(const uint8_t* data, uint32_t length) :
    data(data), length(length), pos(0), count(0), current(0) {
}

uint16_t RLEDecoder::next() {
    if (count == 0) {
        // Read the next value from the RLE stream
        if (pos >= length) {
            // End of stream
            return lilka::colors::Yellow;
        }
        count = data[pos++];
        if (count == 0) {
            // This is bullshit. The count should never be zero. The user has messed up the data. I don't want to deal with this. /AD
            return lilka::colors::Black;
        }
        uint8_t lo = data[pos++];
        uint8_t hi = data[pos++];
        current = (hi << 8) | lo;
    }
    count--;
    return current;
}

Display display;

} // namespace lilka
