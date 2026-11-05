#include "Painter.h"

#include <cstring>

namespace hbui {

Painter::Painter(Adafruit_GFX& gfx, const UiStyle& style) : gfx_(gfx), style_(style) {
    gfx_.cp437(true);
    gfx_.setTextWrap(false);
}

void Painter::clear() { gfx_.fillScreen(color::kBg); }

void Painter::fill(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t c) {
    if (w > 0 && h > 0) {
        gfx_.fillRect(x, y, w, h, c);
    }
}

void Painter::frame(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t thick, uint8_t c) {
    for (uint8_t i = 0; i < thick; ++i) {
        gfx_.drawRect(static_cast<int16_t>(x + i), static_cast<int16_t>(y + i),
                      static_cast<int16_t>(w - 2 * i), static_cast<int16_t>(h - 2 * i), c);
    }
}

void Painter::hline(int16_t x, int16_t y, int16_t w, uint8_t thick, uint8_t c) {
    fill(x, y, w, thick, c);
}

int16_t Painter::textWidth(const char* s, uint8_t size) {
    const size_t n = std::strlen(s);
    return n == 0 ? 0 : static_cast<int16_t>(n * charWidth(size) - size);
}

int16_t Painter::text(int16_t x, int16_t y, const char* s, uint8_t size, uint8_t c, int16_t maxW) {
    const int16_t limit = (maxW > 0) ? static_cast<int16_t>(x + maxW) : layout::kRight;
    gfx_.setTextSize(size);
    gfx_.setTextColor(c);
    int16_t cx = x;
    for (const char* p = s; *p != '\0'; ++p) {
        // karakter digambar 5*size, sel 6*size; cukup kalau gambarnya muat
        if (cx + 5 * size > limit) {
            break;
        }
        char ch = *p;
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<char>(ch - 'a' + 'A');
        }
        gfx_.drawChar(cx, y, static_cast<unsigned char>(ch), c, c, size);
        cx = static_cast<int16_t>(cx + charWidth(size));
    }
    return cx;
}

void Painter::textRight(int16_t right, int16_t y, const char* s, uint8_t size, uint8_t c) {
    const int16_t w = textWidth(s, size);
    text(static_cast<int16_t>(right - w), y, s, size, c, static_cast<int16_t>(w + size));
}

void Painter::textCenter(int16_t centerX, int16_t y, const char* s, uint8_t size, uint8_t c) {
    const int16_t w = textWidth(s, size);
    text(static_cast<int16_t>(centerX - w / 2), y, s, size, c, static_cast<int16_t>(w + size));
}

void Painter::icon(Icon icon, int16_t x, int16_t y, uint8_t c, bool large) {
    if (large) {
        const uint16_t* rows = iconLarge(icon);
        for (int16_t r = 0; r < kIconLargeH; ++r) {
            for (int16_t b = 0; b < kIconLargeW; ++b) {
                if (rows[r] & (0x8000u >> b)) {
                    gfx_.drawPixel(static_cast<int16_t>(x + b), static_cast<int16_t>(y + r), c);
                }
            }
        }
        return;
    }
    const uint8_t* rows = iconSmall(icon);
    for (int16_t r = 0; r < kIconSmallH; ++r) {
        for (int16_t b = 0; b < kIconSmallW; ++b) {
            if (rows[r] & (0x80u >> b)) {
                gfx_.drawPixel(static_cast<int16_t>(x + b), static_cast<int16_t>(y + r), c);
            }
        }
    }
}

void Painter::clearParking() {
    fill(layout::kParkX, layout::kParkY, layout::kParkW, layout::kParkH, color::kBg);
}

}  // namespace hbui
