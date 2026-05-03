#pragma once
// Заглушка SPI. SPIClass SPI; определяется в entry.cpp.

#include <cstdint>

#define SPI_MODE0 0

class SPISettings {
public:
    SPISettings() = default;
    SPISettings(uint32_t, uint8_t, uint8_t) {}
};

class SPIClass {
public:
    void begin(int = -1, int = -1, int = -1, int = -1) {}
    void end() {}
    void beginTransaction(SPISettings) {}
    void endTransaction() {}
    uint8_t transfer(uint8_t) { return 0; }
};

extern SPIClass SPI;
