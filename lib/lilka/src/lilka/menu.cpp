#include "ui.h"

// Mono port: composite a 1bpp canvas onto any GFX target with color-key
// semantics. Pixels whose mono bit differs from the (thresholded) background
// are drawn; matching pixels are skipped, like the old transparent-color draw.
static void drawMonoCanvasTran(
    Arduino_GFX* g, lilka::Canvas* src, int16_t destX, int16_t destY, int16_t w, int16_t h, uint16_t bgColor
) {
    const bool bgBit = bgColor & 0b1000010000010000;
    const uint8_t* fb = src->getFramebuffer();
    const int16_t stride = src->monoStride();
    if (w > src->width()) w = src->width();
    if (h > src->height()) h = src->height();
    for (int16_t y = 0; y < h; y++) {
        const uint8_t* line = fb + y * stride;
        for (int16_t x = 0; x < w; x++) {
            const bool bit = line[x >> 3] & (0x80 >> (x & 7));
            if (bit != bgBit) {
                g->writePixel(destX + x, destY + y, bit ? 0xFFFF : 0x0000);
            }
        }
    }
}

namespace lilka {

#define MENU_HEIGHT 6

#define MIN(a, b)   ((a) < (b) ? (a) : (b))

Menu::Menu(const String& title) {
    this->title = title;
    this->scroll = 0;
    this->setCursor(0);
    this->done = false;
    this->iconImage = new Image(menu_icon_width, menu_icon_height, bgColor, menu_icon_width / 2, menu_icon_height / 2);
    this->iconCanvas = new Canvas(menu_icon_width, menu_icon_height);
    this->lastCursorMove = millis();
    this->lastCursorMoveUs = micros();
    this->button = Button::COUNT;
    this->activationButtons.push_back(Button::A);
    this->firstRender = -1;
}

Menu::~Menu() {
    delete iconImage;
    delete iconCanvas;
}
void Menu::setTitle(const String& title) {
    this->title = title;
}
void Menu::addItem(
    const String& title, const menu_icon_t* icon, uint16_t color, const String& postfix, PMenuItemCallback callback,
    void* callbackData
) {
    items.push_back(
        {.title = title,
         .icon = icon,
         .color = color,
         .postfix = postfix,
         .callback = callback,
         .callbackData = callbackData}
    );
}

void Menu::setCursor(int16_t cursor) {
    if (cursor < 0) {
        cursor = items.size() - 1;
    } else if (cursor >= items.size()) {
        cursor = 0;
    }
    this->cursor = cursor;
}

void Menu::update() {
    State state = controller.getState();
    auto cursorLastPos = cursor;
    if (state.up.justPressed) {
        // Move cursor up
        if (cursor == 0) {
            cursor = items.size() - 1;
        } else {
            cursor--;
        }
    } else if (state.down.justPressed) {
        // Move cursor down
        cursor++;
        if (cursor >= items.size()) {
            cursor = 0;
        }
    } else if (state.left.justPressed) {
        // Scroll PageUp
        if (cursor == 0) cursor = items.size() - 1;
        else cursor = (cursor - MENU_HEIGHT) <= 0 ? cursor = 0 : (cursor - MENU_HEIGHT);

    } else if (state.right.justPressed) {
        // Scroll PageDown
        if (cursor == items.size() - 1) cursor = 0;
        else cursor = (cursor + MENU_HEIGHT) >= items.size() - 1 ? items.size() - 1 : (cursor + MENU_HEIGHT);
    }
    if (cursorLastPos != cursor) lastCursorMoveUs = micros();
    if (cursor < scroll) {
        scroll = cursor;
    } else if (cursor > scroll + MENU_HEIGHT - 1) {
        scroll = cursor - MENU_HEIGHT + 1;
    }

    for (Button activationButton : activationButtons) {
        lilka::_StateButtons& buttonsArray = *reinterpret_cast<lilka::_StateButtons*>(&state);
        if (buttonsArray[activationButton].justPressed) {
            button = activationButton;
            done = true;
            // Should be made after done flag setup to allow to clear it by isFinished() call
            if (items[cursor].callback) { // call callback if
                items[cursor].callback(items[cursor].callbackData);
            }
        }
    }
    vTaskDelay(LILKA_UI_UPDATE_DELAY_MS / portTICK_PERIOD_MS);
}

// Scroll marquee back and forth based on time, pausing at start and end
int16_t calculateMarqueeShift(uint64_t time, uint16_t maxShift, uint16_t pixelsPerSecond) {
    constexpr uint64_t startDelay = 750;
    uint64_t midTime = maxShift * 1000 / pixelsPerSecond;
    constexpr uint64_t endDelay = 750;
    const uint64_t period = startDelay + 2 * midTime + endDelay;
    const uint64_t phase = time % period;

    int16_t shift = 0;
    if (phase < startDelay) {
        // Start delay
        shift = 0;
    } else if (phase < startDelay + midTime) {
        // Scroll
        shift = (phase - startDelay) * pixelsPerSecond / 1000;
    } else if (phase < startDelay + midTime + endDelay) {
        // End delay
        shift = maxShift;
    } else {
        // Scroll back
        shift = maxShift - (phase - startDelay - midTime - endDelay) * pixelsPerSecond / 1000;
    }
    return -shift;
}

void Menu::draw(Arduino_GFX* canvas) {
    if (this->firstRender == -1) {
        this->firstRender = millis();
    }

    constexpr int16_t scrollbarWidth = 8;
    constexpr int16_t scrollbarLeftPadding = 4;
    constexpr int16_t postfixLeftPadding = 4;
    constexpr int16_t iconWidth = 32;
    constexpr int16_t titleTextHeight = 32;
    constexpr int16_t itemsY = 64;
    constexpr int16_t itemHeight = menu_item_height;
    uint16_t menu_size = items.size();
    const bool needsScrollbar = menu_size > MENU_HEIGHT;

    // Clear screen and draw graphical decorations
    canvas->fillScreen(bgColor);
    // int8_t angleShift = sin(millis() / 1000.0) * 16;
    // // Draw triangle in top-left
    // canvas->fillTriangle(0, 0, 48 - angleShift, 0, 0, 48 + angleShift, lilka::colors::Black);
    // // Draw triangle in top-right
    // canvas->fillTriangle(
    //     canvas->width(),
    //     0,
    //     canvas->width() - 48 - angleShift,
    //     0,
    //     canvas->width(),
    //     48 - angleShift,
    //     lilka::colors::Black
    // );

    // Draw title text
    const uint16_t titleWidth = getTextWidth(FONT_5x7, title.c_str()) * 2;
    const uint16_t titleWidthAvailable = canvas->width() - 64;
    if (titleWidth > titleWidthAvailable) {
        // Marquee
        Canvas marquee(titleWidthAvailable, titleTextHeight + 8);
        marquee.fillScreen(bgColor);
        marquee.setFont(FONT_5x7);
        marquee.setTextSize(2);
        marquee.setCursor(
            calculateMarqueeShift(millis() - firstRender, titleWidth - titleWidthAvailable, 50), titleTextHeight
        );
        marquee.setTextColor(lilka::colors::Black);
        marquee.println(title);
        drawMonoCanvasTran(canvas, &marquee, 32, 0, marquee.width(), marquee.height(), bgColor);
    } else {
        // Text fits
        canvas->setFont(FONT_5x7);
        canvas->setTextSize(2);
        canvas->setCursor(32, 40);
        canvas->setTextColor(lilka::colors::Black);
        canvas->setTextBound(32, 8, titleWidthAvailable, titleTextHeight);
        canvas->println(title);
    }

    // Highlight selected item
    constexpr int16_t highlightRadius = 7;
    canvas->fillRoundRect(
        32,
        (cursor - scroll) * itemHeight + (itemsY - itemHeight) - 0.5 * (itemHeight - menu_icon_height),
        titleWidthAvailable,
        itemHeight,
        highlightRadius,
        lilka::colors::Black
    );

    // Draw item icons and text
    for (int i = scroll; i < MIN(scroll + MENU_HEIGHT, menu_size); i++) {
        int16_t screenI = i - scroll;
        const menu_icon_t* icon = items[i].icon;
        canvas->setTextBound(0, itemsY + screenI * itemHeight - itemHeight, canvas->width(), itemHeight);
        
        
        // Draw icon if it exists
        if (icon) {
            memcpy(iconImage->pixels, *icon, sizeof(menu_icon_t));
            if (cursor == i) {
                // invert icon
                const uint8_t* src = reinterpret_cast<const uint8_t*>(*icon);
                uint8_t* dst = reinterpret_cast<uint8_t*>(iconImage->pixels);
                std::transform(src, src + sizeof(menu_icon_t), dst, [](uint8_t b) { return static_cast<uint8_t>(~b); });
                
                // add rotation animation to icon
                float elapsedMs = (micros() - lastCursorMoveUs) * 0.001f;
                Transform t = Transform().rotate(sinf(elapsedMs * PI / 1000.0f) * 3.0f);
                iconCanvas->fillScreen(lilka::colors::White);
                iconCanvas->drawImageTransformed(iconImage, 12, 12, t);
                drawMonoCanvasTran(
                    canvas, iconCanvas, 36, itemsY + screenI * itemHeight - itemHeight, menu_icon_width, menu_icon_height,
                    lilka::colors::Black
                );
            } else {
                canvas->draw16bitRGBBitmapWithTranColor(
                    34,
                    itemsY + screenI * itemHeight - itemHeight,
                    const_cast<uint16_t*>(*icon),
                    lilka::colors::White,
                    menu_icon_width,
                    menu_icon_height
                );
            }
        }

        // Draw item title, inverting text for current item
        canvas->setTextColor(cursor == i ? lilka::colors::White : lilka::colors::Black);
        
        uint16_t postfixWidth = 0;
        if (items[i].postfix.length()) {
            canvas->setTextSize(2);
            canvas->setFont(FONT_5x7);
            
            // Calculate postfix width
            int16_t x1, y1;
            uint16_t h;
            (void)x1;
            (void)y1;
            (void)h;
            canvas->setTextBound(70, 0, canvas->width(), canvas->height());
            canvas->getTextBounds(items[i].postfix, 0, 0, &x1, &y1, &postfixWidth, &h);
            canvas->setCursor(
                canvas->width() - postfixWidth - postfixLeftPadding - 4, itemsY + screenI * itemHeight - 10
            );
            canvas->println(items[i].postfix);
        }

        int16_t widthAvailable = canvas->width() - 78 - postfixWidth - postfixLeftPadding;
        if (widthAvailable < 0) {
            // No space for title
            continue;
        }

        uint16_t nameWidth = (getTextWidth(FONT_5x7, items[i].title.c_str()) + 1) * 2;
        if (nameWidth > widthAvailable && cursor == i) {
            // Marquee - only exists on cursor row, so render black-on-white
            Canvas marquee(widthAvailable, itemHeight);
            marquee.fillScreen(lilka::colors::Black);
            marquee.setFont(FONT_5x7);
            marquee.setTextSize(2);
            marquee.setTextWrap(false);
            marquee.setCursor(calculateMarqueeShift(millis() - lastCursorMoveUs, nameWidth - widthAvailable, 50), 20);
            marquee.setTextColor(lilka::colors::White);
            marquee.println(items[i].title);
            drawMonoCanvasTran(canvas, &marquee, 70, 10, widthAvailable, itemHeight,
                lilka::colors::Red);
            drawMonoCanvasTran(
                canvas, &marquee, 70, itemsY + (screenI - 1) * itemHeight - 0.5 * (itemHeight - menu_icon_height), widthAvailable, marquee.height(),
                lilka::colors::Black
            );
        } else {
            // Draw text if fits otherwise truncate
            canvas->setTextSize(2);
            canvas->setFont(FONT_5x7);
            canvas->setCursor(38+iconWidth, itemsY + screenI * itemHeight - 10);
            canvas->setTextWrap(false);
            canvas->setTextBound(iconWidth, itemsY + screenI * itemHeight - 20, widthAvailable + 40, itemHeight);
            canvas->println(items[i].title);
        }
    }

    // Draw scrollbar
    if (needsScrollbar) {
        int top = itemsY - 20;
        int height = MENU_HEIGHT * itemHeight;
        canvas->fillRect(canvas->width() - 13, top, 2, height, lilka::colors::Black);
        canvas->fillRect(canvas->width() - 16, top, 8, 2, lilka::colors::Black);
        canvas->fillRect(canvas->width() - 16, top + height - 2, 8, 2, lilka::colors::Black);
        int barHeight = height * MENU_HEIGHT / menu_size;
        int barTop = top + scroll * height / menu_size;
        canvas->fillRect(canvas->width() - 16, barTop, 8, barHeight, lilka::colors::Black);
    }
}

bool Menu::isFinished() {
    if (done) {
        done = false;
        return true;
    }
    return false;
}

int16_t Menu::getCursor() {
    return cursor;
}

void Menu::setColor(uint16_t color) {
    this->color = color;
    // Idea is actually almost incredible, a single problem is that we can't mention default
    // color in Menu.addItem, so, maybe better not to :)
    // for (auto& item : items) {
    //     item.color = color;
    // }
}

void Menu::setBackgroundColor(uint16_t color) {
    this->bgColor = color;
}

bool Menu::setItem(int16_t index, const String& title, const menu_icon_t* icon, uint16_t color, const String& postfix) {
    if (index > items.size() - 1) {
        return false;
    } else {
        items[index].title = title;
        items[index].icon = icon;
        items[index].color = color;
        items[index].postfix = postfix;
        return true;
    }
}

bool Menu::getItem(int16_t index, MenuItem* menuItem) {
    if ((menuItem == NULL) || index > items.size() - 1 || index < 0) {
        return false;
    } else {
        *menuItem = items[index];
        return true;
    }
}

void Menu::clearItems() {
    setCursor(0);
    items.clear();
}

int16_t Menu::getItemCount() {
    return items.size();
}

void Menu::addActivationButton(Button activationButton) {
    // Handle if button already added
    if (std::find(activationButtons.begin(), activationButtons.end(), activationButton) != activationButtons.end())
        return;

    activationButtons.push_back(activationButton);
}

void Menu::removeActivationButton(Button activationButton) {
    activationButtons.erase(
        std::remove(activationButtons.begin(), activationButtons.end(), activationButton), activationButtons.end()
    );
}

Button Menu::getButton() {
    return button;
}

} // namespace lilka
