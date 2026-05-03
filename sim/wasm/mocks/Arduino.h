#pragma once
// ================================================================
// Arduino.h — mock для компиляции ESP32-прошивки через Emscripten.
// Подменяет настоящий Arduino.h при добавлении mocks/ первым в -I.
// ================================================================

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdarg>
#include <algorithm>
#include <ctime>
#include <string>

// ── M_PI / DEG_TO_RAD ──────────────────────────────────────────
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD 0.017453292519943295  // M_PI / 180.0
#endif

// ── Типы ────────────────────────────────────────────────────────
typedef uint8_t byte;
typedef bool boolean;

// ── PROGMEM и pgm_read_* ───────────────────────────────────────
#define PROGMEM
#define pgm_read_byte(addr)  (*(const uint8_t  *)(addr))
#define pgm_read_word(addr)  (*(const uint16_t *)(addr))
#define pgm_read_dword(addr) (*(const uint32_t *)(addr))
#define pgm_read_ptr(addr)   (*(const void *const *)(addr))

// ── F() / __FlashStringHelper ───────────────────────────────────
#define __FlashStringHelper char
#define F(x) (x)

// ── GPIO ────────────────────────────────────────────────────────
#define INPUT         0x01
#define OUTPUT        0x02
#define INPUT_PULLUP  0x05
#define HIGH          1
#define LOW           0

// ── min / max (шаблоны, не макросы) ─────────────────────────────
template<typename T> inline const T& _arduino_min(const T& a, const T& b) { return (a < b) ? a : b; }
template<typename T> inline const T& _arduino_max(const T& a, const T& b) { return (a > b) ? a : b; }
#ifndef min
#define min(a, b) _arduino_min(a, b)
#endif
#ifndef max
#define max(a, b) _arduino_max(a, b)
#endif

using std::round;
using std::abs;

// ── millis / delay (заглушки) ───────────────────────────────────
inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}

// ── analogRead (заглушка) ───────────────────────────────────────
inline int analogRead(int) { return 0; }
inline int analogReadMilliVolts(int) { return 0; }
inline void pinMode(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline void digitalWrite(int, int) {}

// ── String ──────────────────────────────────────────────────────
class String {
    std::string buf;
public:
    String() = default;
    String(const char* s) : buf(s ? s : "") {}
    String(const std::string& s) : buf(s) {}
    String(int v) : buf(std::to_string(v)) {}
    String(long v) : buf(std::to_string(v)) {}
    String(unsigned int v) : buf(std::to_string(v)) {}
    String(unsigned long v) : buf(std::to_string(v)) {}
    String(float v, int dec = 2) {
        char b[32];
        snprintf(b, sizeof(b), "%.*f", dec, static_cast<double>(v));
        buf = b;
    }
    String(double v, int dec = 2) {
        char b[32];
        snprintf(b, sizeof(b), "%.*f", dec, v);
        buf = b;
    }
    String(char c) : buf(1, c) {}

    const char* c_str() const { return buf.c_str(); }
    unsigned int length() const { return static_cast<unsigned int>(buf.length()); }
    bool isEmpty() const { return buf.empty(); }

    String operator+(const String& o) const { return String(std::string(buf + o.buf)); }
    String operator+(const char* s) const { return String(std::string(buf + (s ? s : ""))); }
    String operator+(char c) const { std::string r = buf; r += c; return String(r); }
    String& operator+=(const String& o) { buf += o.buf; return *this; }
    String& operator+=(const char* s) { if (s) buf += s; return *this; }
    String& operator+=(char c) { buf += c; return *this; }

    bool operator==(const String& o) const { return buf == o.buf; }
    bool operator==(const char* s) const { return buf == (s ? s : ""); }
    bool operator!=(const String& o) const { return buf != o.buf; }
    bool operator!=(const char* s) const { return !(*this == s); }

    friend String operator+(const char* a, const String& b) {
        return String(std::string(a ? a : "") + b.buf);
    }

    bool startsWith(const String& prefix) const {
        return buf.rfind(prefix.buf, 0) == 0;
    }
    bool endsWith(const String& suffix) const {
        if (suffix.buf.size() > buf.size()) return false;
        return buf.compare(buf.size() - suffix.buf.size(), suffix.buf.size(), suffix.buf) == 0;
    }
    int indexOf(char c) const {
        auto p = buf.find(c);
        return p == std::string::npos ? -1 : static_cast<int>(p);
    }
    int indexOf(const String& s) const {
        auto p = buf.find(s.buf);
        return p == std::string::npos ? -1 : static_cast<int>(p);
    }
    String substring(unsigned int from) const {
        if (from >= buf.size()) return String();
        return String(std::string(buf.substr(from)));
    }
    String substring(unsigned int from, unsigned int to) const {
        if (from >= buf.size()) return String();
        return String(std::string(buf.substr(from, to - from)));
    }
    bool includes(const String& s) const {
        return buf.find(s.buf) != std::string::npos;
    }
    void toLowerCase() {
        for (auto& c : buf) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
    }
    void toUpperCase() {
        for (auto& c : buf) c = static_cast<char>(::toupper(static_cast<unsigned char>(c)));
    }
    void trim() {
        auto start = buf.find_first_not_of(" \t\r\n");
        auto end   = buf.find_last_not_of(" \t\r\n");
        if (start == std::string::npos) { buf.clear(); return; }
        buf = buf.substr(start, end - start + 1);
    }
    int toInt() const { return std::atoi(buf.c_str()); }
    float toFloat() const { return std::strtof(buf.c_str(), nullptr); }
    double toDouble() const { return std::strtod(buf.c_str(), nullptr); }

    char operator[](unsigned int i) const { return buf[i]; }
    char& operator[](unsigned int i) { return buf[i]; }

    void reserve(unsigned int size) { buf.reserve(size); }
    void remove(unsigned int index) { buf.erase(index); }
    void remove(unsigned int index, unsigned int count) { buf.erase(index, count); }
    void replace(const String& from, const String& to) {
        size_t pos = 0;
        while ((pos = buf.find(from.buf, pos)) != std::string::npos) {
            buf.replace(pos, from.buf.length(), to.buf);
            pos += to.buf.length();
        }
    }

    operator std::string() const { return buf; }
};

// ── Print ───────────────────────────────────────────────────────
class Print {
public:
    virtual ~Print() = default;
    virtual size_t write(uint8_t) { return 1; }
    virtual size_t write(const uint8_t* buf, size_t size) {
        for (size_t i = 0; i < size; i++) write(buf[i]);
        return size;
    }
    size_t print(const char*) { return 0; }
    size_t print(const String&) { return 0; }
    size_t print(int) { return 0; }
    size_t print(unsigned int) { return 0; }
    size_t print(long) { return 0; }
    size_t print(unsigned long) { return 0; }
    size_t print(float, int = 2) { return 0; }
    size_t print(double, int = 2) { return 0; }
    size_t println() { return 0; }
    size_t println(const char*) { return 0; }
    size_t println(const String&) { return 0; }
    size_t println(int) { return 0; }
    size_t println(unsigned int) { return 0; }
    size_t println(long) { return 0; }
    size_t println(unsigned long) { return 0; }
    size_t println(float, int = 2) { return 0; }
    size_t println(double, int = 2) { return 0; }
    size_t printf(const char* fmt, ...) { (void)fmt; return 0; }
};

// ── HardwareSerial / Serial ─────────────────────────────────────
// Serial определяется в entry.cpp: HardwareSerial Serial;
class HardwareSerial : public Print {
public:
    void begin(unsigned long) {}
    void begin(unsigned long, uint32_t) {}
    void flush() {}
    int available() { return 0; }
    int read() { return -1; }
    operator bool() const { return true; }
};
extern HardwareSerial Serial;

// ── HWCDC (USB CDC заглушка для ESP32-S3) ───────────────────────
class HWCDC {
public:
    static bool isPlugged() { return false; }
};

// ── RTC_DATA_ATTR (NOP на десктопе) ─────────────────────────────
#define RTC_DATA_ATTR

// ── ESP (заглушка) ──────────────────────────────────────────────
class EspClass {
public:
    void restart() {}
    uint32_t getFreeHeap() { return 65536; }
    uint32_t getFreePsram() { return 0; }
};
extern EspClass ESP;

// ── WiFi status codes (заглушки) ────────────────────────────────
#define WL_CONNECTED 3

// ── gmtime_r (POSIX, доступна в Emscripten) ────────────────────
// gmtime_r уже есть в <ctime> на POSIX/Emscripten. На Windows
// используем gmtime_s wrapper:
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
inline struct tm* gmtime_r(const time_t* t, struct tm* result) {
    return gmtime_s(result, t) == 0 ? result : nullptr;
}
#endif
