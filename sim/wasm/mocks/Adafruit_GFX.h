#pragma once
// ================================================================
// Adafruit_GFX.h — mock для Emscripten.
// Полностью header-only реализация графических примитивов.
// Алгоритмы портированы из sim/src/gfx.ts (Bresenham, midpoint
// circle, scanline triangle fill) — идентичны оригиналу Adafruit.
// ================================================================

#include "Arduino.h"

// GFXfont (не используется display_renderer, но может быть в сигнатурах)
#ifndef _GFXFONT_H_
#define _GFXFONT_H_
typedef struct {
    uint8_t  *bitmap;
    void     *glyph;
    uint16_t  first, last;
    uint8_t   yAdvance;
} GFXfont;
#endif

class Adafruit_GFX : public Print {
public:
    Adafruit_GFX(int16_t w, int16_t h) : _width(w), _height(h) {}
    virtual ~Adafruit_GFX() = default;

    virtual void drawPixel(int16_t x, int16_t y, uint16_t color) = 0;

    int16_t width()  const { return _width;  }
    int16_t height() const { return _height; }

    virtual void setRotation(uint8_t r) { rotation = r % 4; }
    uint8_t getRotation() const { return rotation; }

    // ── Линии (Bresenham) ───────────────────────────────────────
    inline void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
        // Быстрые пути для горизонтальных/вертикальных линий
        if (y0 == y1) { drawFastHLine(x0 < x1 ? x0 : x1, y0, ::abs(x1 - x0) + 1, color); return; }
        if (x0 == x1) { drawFastVLine(x0, y0 < y1 ? y0 : y1, ::abs(y1 - y0) + 1, color); return; }

        bool steep = ::abs(y1 - y0) > ::abs(x1 - x0);
        if (steep)   { _swap(x0, y0); _swap(x1, y1); }
        if (x0 > x1) { _swap(x0, x1); _swap(y0, y1); }

        int16_t dx = x1 - x0;
        int16_t dy = static_cast<int16_t>(::abs(y1 - y0));
        int16_t err = dx >> 1;
        int16_t ystep = (y0 < y1) ? 1 : -1;
        int16_t y = y0;

        for (int16_t x = x0; x <= x1; x++) {
            if (steep) drawPixel(y, x, color);
            else       drawPixel(x, y, color);
            err -= dy;
            if (err < 0) {
                y += ystep;
                err += dx;
            }
        }
    }

    inline void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t color) {
        for (int16_t i = 0; i < w; i++) drawPixel(x + i, y, color);
    }

    inline void drawFastVLine(int16_t x, int16_t y, int16_t h, uint16_t color) {
        for (int16_t i = 0; i < h; i++) drawPixel(x, y + i, color);
    }

    // ── Прямоугольники ──────────────────────────────────────────
    inline void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        drawFastHLine(x, y, w, color);
        drawFastHLine(x, y + h - 1, w, color);
        drawFastVLine(x, y, h, color);
        drawFastVLine(x + w - 1, y, h, color);
    }

    inline void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
        for (int16_t j = 0; j < h; j++) {
            drawFastHLine(x, y + j, w, color);
        }
    }

    inline void fillScreen(uint16_t color) {
        fillRect(0, 0, _width, _height, color);
    }

    // ── Окружности (midpoint algorithm) ─────────────────────────
    inline void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
        int16_t f     = 1 - r;
        int16_t ddF_x = 1;
        int16_t ddF_y = -2 * r;
        int16_t x = 0;
        int16_t y = r;

        drawPixel(x0,     y0 + r, color);
        drawPixel(x0,     y0 - r, color);
        drawPixel(x0 + r, y0,     color);
        drawPixel(x0 - r, y0,     color);

        while (x < y) {
            if (f >= 0) {
                y--;
                ddF_y += 2;
                f += ddF_y;
            }
            x++;
            ddF_x += 2;
            f += ddF_x;

            drawPixel(x0 + x, y0 + y, color);
            drawPixel(x0 - x, y0 + y, color);
            drawPixel(x0 + x, y0 - y, color);
            drawPixel(x0 - x, y0 - y, color);
            drawPixel(x0 + y, y0 + x, color);
            drawPixel(x0 - y, y0 + x, color);
            drawPixel(x0 + y, y0 - x, color);
            drawPixel(x0 - y, y0 - x, color);
        }
    }

    inline void drawCircleHelper(int16_t x0, int16_t y0, int16_t r,
                                  uint8_t cornername, uint16_t color) {
        int16_t f     = 1 - r;
        int16_t ddF_x = 1;
        int16_t ddF_y = -2 * r;
        int16_t x = 0;
        int16_t y = r;

        while (x < y) {
            if (f >= 0) {
                y--;
                ddF_y += 2;
                f += ddF_y;
            }
            x++;
            ddF_x += 2;
            f += ddF_x;

            if (cornername & 0x4) {
                drawPixel(x0 + x, y0 + y, color);
                drawPixel(x0 + y, y0 + x, color);
            }
            if (cornername & 0x2) {
                drawPixel(x0 + x, y0 - y, color);
                drawPixel(x0 + y, y0 - x, color);
            }
            if (cornername & 0x8) {
                drawPixel(x0 - y, y0 + x, color);
                drawPixel(x0 - x, y0 + y, color);
            }
            if (cornername & 0x1) {
                drawPixel(x0 - y, y0 - x, color);
                drawPixel(x0 - x, y0 - y, color);
            }
        }
    }

    inline void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
        drawFastVLine(x0, y0 - r, 2 * r + 1, color);
        fillCircleHelper(x0, y0, r, 3, 0, color);
    }

    inline void fillCircleHelper(int16_t x0, int16_t y0, int16_t r,
                                  uint8_t corners, int16_t delta, uint16_t color) {
        int16_t f     = 1 - r;
        int16_t ddF_x = 1;
        int16_t ddF_y = -2 * r;
        int16_t x = 0;
        int16_t y = r;
        int16_t px = x;
        int16_t py = y;

        // Adafruit GFX: +1 чтобы заполнить центральную строку
        delta++;

        while (x < y) {
            if (f >= 0) {
                y--;
                ddF_y += 2;
                f += ddF_y;
            }
            x++;
            ddF_x += 2;
            f += ddF_x;

            if (x < (y + 1)) {
                if (corners & 1) drawFastVLine(x0 + x, y0 - y, 2 * y + delta, color);
                if (corners & 2) drawFastVLine(x0 - x, y0 - y, 2 * y + delta, color);
            }
            if (y != py) {
                if (corners & 1) drawFastVLine(x0 + py, y0 - px, 2 * px + delta, color);
                if (corners & 2) drawFastVLine(x0 - py, y0 - px, 2 * px + delta, color);
                py = y;
            }
            px = x;
        }
    }

    // ── Треугольники ────────────────────────────────────────────
    inline void drawTriangle(int16_t x0, int16_t y0,
                              int16_t x1, int16_t y1,
                              int16_t x2, int16_t y2, uint16_t color) {
        drawLine(x0, y0, x1, y1, color);
        drawLine(x1, y1, x2, y2, color);
        drawLine(x2, y2, x0, y0, color);
    }

    inline void fillTriangle(int16_t x0, int16_t y0,
                              int16_t x1, int16_t y1,
                              int16_t x2, int16_t y2, uint16_t color) {
        // Сортировка вершин по Y (Adafruit GFX алгоритм)
        if (y0 > y1) { _swap(x0, x1); _swap(y0, y1); }
        if (y1 > y2) { _swap(x1, x2); _swap(y1, y2); }
        if (y0 > y1) { _swap(x0, x1); _swap(y0, y1); }

        // Вырожденный случай: все точки на одной горизонтали
        if (y0 == y2) {
            int16_t a = x0, b = x0;
            if (x1 < a) a = x1; else if (x1 > b) b = x1;
            if (x2 < a) a = x2; else if (x2 > b) b = x2;
            drawFastHLine(a, y0, b - a + 1, color);
            return;
        }

        int16_t dx01 = x1 - x0, dy01 = y1 - y0;
        int16_t dx02 = x2 - x0, dy02 = y2 - y0;
        int16_t dx12 = x2 - x1, dy12 = y2 - y1;
        int32_t sa = 0, sb = 0;

        // Верхняя половина
        int16_t last = (y1 == y2) ? y1 : (int16_t)(y1 - 1);
        int16_t y;
        for (y = y0; y <= last; y++) {
            int16_t a = static_cast<int16_t>(x0 + sa / dy01);
            int16_t b = static_cast<int16_t>(x0 + sb / dy02);
            sa += dx01;
            sb += dx02;
            if (a > b) _swap(a, b);
            drawFastHLine(a, y, b - a + 1, color);
        }

        // Нижняя половина
        sa = static_cast<int32_t>(dx12) * (y - y1);
        sb = static_cast<int32_t>(dx02) * (y - y0);
        for (; y <= y2; y++) {
            int16_t a = static_cast<int16_t>(x1 + sa / dy12);
            int16_t b = static_cast<int16_t>(x0 + sb / dy02);
            sa += dx12;
            sb += dx02;
            if (a > b) _swap(a, b);
            drawFastHLine(a, y, b - a + 1, color);
        }
    }

    // ── Скруглённые прямоугольники ──────────────────────────────
    inline void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                               int16_t r, uint16_t color) {
        int16_t max_r = _arduino_min(w, h) / 2;
        if (r > max_r) r = max_r;
        drawFastHLine(x + r, y,         w - 2 * r, color);          // верх
        drawFastHLine(x + r, y + h - 1, w - 2 * r, color);          // низ
        drawFastVLine(x,         y + r, h - 2 * r, color);          // лево
        drawFastVLine(x + w - 1, y + r, h - 2 * r, color);          // право
        drawCircleHelper(x + r,         y + r,         r, 1, color); // верх-лево
        drawCircleHelper(x + w - r - 1, y + r,         r, 2, color); // верх-право
        drawCircleHelper(x + w - r - 1, y + h - r - 1, r, 4, color); // низ-право
        drawCircleHelper(x + r,         y + h - r - 1, r, 8, color); // низ-лево
    }

    inline void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h,
                                int16_t r, uint16_t color) {
        int16_t max_r = _arduino_min(w, h) / 2;
        if (r > max_r) r = max_r;
        fillRect(x + r, y, w - 2 * r, h, color);
        fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2 * r - 1, color);
        fillCircleHelper(x + r,         y + r, r, 2, h - 2 * r - 1, color);
    }

    // ── Битмапы (row-major, MSB first) ──────────────────────────
    // Вариант с одним цветом: рисует пиксели только для установленных бит
    inline void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                            int16_t w, int16_t h, uint16_t color) {
        int16_t bytesPerRow = (w + 7) / 8;
        for (int16_t j = 0; j < h; j++) {
            for (int16_t i = 0; i < w; i++) {
                uint8_t b = pgm_read_byte(&bitmap[j * bytesPerRow + (i >> 3)]);
                if (b & (0x80 >> (i & 7))) {
                    drawPixel(x + i, y + j, color);
                }
            }
        }
    }

    // Вариант с foreground/background: рисует оба цвета
    inline void drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                            int16_t w, int16_t h, uint16_t color, uint16_t bg) {
        int16_t bytesPerRow = (w + 7) / 8;
        for (int16_t j = 0; j < h; j++) {
            for (int16_t i = 0; i < w; i++) {
                uint8_t b = pgm_read_byte(&bitmap[j * bytesPerRow + (i >> 3)]);
                if (b & (0x80 >> (i & 7))) {
                    drawPixel(x + i, y + j, color);
                } else {
                    drawPixel(x + i, y + j, bg);
                }
            }
        }
    }

    // Cursor / font API (заглушки — шрифты через U8g2)
    void setCursor(int16_t, int16_t) {}
    void setTextColor(uint16_t) {}
    void setTextColor(uint16_t, uint16_t) {}
    void setTextSize(uint8_t) {}
    void setTextWrap(bool) {}
    void setFont(const GFXfont*) {}

protected:
    int16_t _width, _height;
    uint8_t rotation = 0;

private:
    template<typename T>
    static inline void _swap(T& a, T& b) { T t = a; a = b; b = t; }
};
