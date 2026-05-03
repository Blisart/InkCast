#pragma once
// Заглушка NVS Preferences для ESP32. Все операции — NOP.

#include "Arduino.h"

class Preferences {
public:
    bool begin(const char*, bool = false) { return true; }
    void end() {}

    String getString(const char*, const String& def = String()) { return def; }
    int    getInt(const char*, int def = 0)          { return def; }
    float  getFloat(const char*, float def = 0.0f)   { return def; }
    bool   getBool(const char*, bool def = false)     { return def; }

    size_t putString(const char*, const String&) { return 1; }
    size_t putInt(const char*, int)              { return 1; }
    size_t putFloat(const char*, float)          { return 1; }
    size_t putBool(const char*, bool)            { return 1; }
    size_t putBytes(const char*, const void*, size_t) { return 1; }
    size_t getBytes(const char*, void*, size_t)  { return 0; }

    bool isKey(const char*) { return false; }
    bool clear()            { return true; }
    bool remove(const char*) { return true; }
};
