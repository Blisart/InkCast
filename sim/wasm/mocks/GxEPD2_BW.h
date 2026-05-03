#pragma once
// ================================================================
// GxEPD2_BW.h — mock e-paper дисплей с 1bpp framebuffer.
// Вместо SPI-вывода на реальный дисплей пиксели хранятся в памяти.
// ================================================================

#include "Adafruit_GFX.h"
#include "SPI.h"

// Цветовые константы (те же, что в реальной GxEPD2)
#define GxEPD_BLACK  0x0000
#define GxEPD_WHITE  0xFFFF

// Заглушка модели дисплея GDEM0397T81P
struct GxEPD2_397_GDEM0397T81 {
    static constexpr int16_t HEIGHT = 480;
};

template<typename GxEPD2Type, int16_t PAGE_HEIGHT>
class GxEPD2_BW : public Adafruit_GFX {
public:
    static constexpr int16_t WIDTH  = 800;
    static constexpr int16_t HEIGHT = PAGE_HEIGHT;
    static constexpr int BYTES_PER_ROW = (WIDTH + 7) / 8;  // 100
    static constexpr int BUF_SIZE = BYTES_PER_ROW * HEIGHT; // 48000

    uint8_t buffer[BUF_SIZE];

    GxEPD2_BW() : Adafruit_GFX(WIDTH, HEIGHT) {
        // Весь буфер = белый (0x00 = все биты сброшены = белые пиксели)
        memset(buffer, 0, BUF_SIZE);
    }

    // Конструктор с пинами (игнорируются в mock)
    GxEPD2_BW(int, int, int, int) : Adafruit_GFX(WIDTH, HEIGHT) {
        memset(buffer, 0, BUF_SIZE);
    }

    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        // Rotation обрабатывается логически: прошивка вызывает setRotation(2),
        // но для Canvas-симулятора координаты уже в правильной ориентации —
        // rotation=2 компенсирует физический поворот e-ink модуля, который
        // в симуляторе отсутствует. Поэтому игнорируем rotation здесь.
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) return;

        int idx = y * BYTES_PER_ROW + (x / 8);
        uint8_t bit = 0x80 >> (x & 7);
        if (color == GxEPD_BLACK) {
            buffer[idx] |= bit;   // установить бит = чёрный
        } else {
            buffer[idx] &= ~bit;  // сбросить бит = белый
        }
    }

    // ── API, используемый прошивкой ─────────────────────────────
    void init(unsigned long = 115200, bool = true, int = 10, bool = false) {}
    void setFullWindow() {}
    void setPartialWindow(int16_t, int16_t, int16_t, int16_t) {}

    // Пагинация: в mock весь экран в одной странице
    bool firstPage() { return true; }
    bool nextPage()  { return false; }

    void hibernate() {}
    void powerOff() {}
    void display(bool = true) {}

    // Очистить буфер (белый)
    void clearBuffer() { memset(buffer, 0, BUF_SIZE); }

    // fillScreen через Adafruit_GFX (переопределяем для скорости)
    void fillScreen(uint16_t color) {
        memset(buffer, (color == GxEPD_BLACK) ? 0xFF : 0x00, BUF_SIZE);
    }
};
