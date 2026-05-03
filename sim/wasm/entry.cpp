// ================================================================
// entry.cpp — WASM точка входа для симулятора e-paper дисплея.
//
// Компилируется Emscripten вместе с реальным display_renderer.cpp
// и U8g2_for_Adafruit_GFX.cpp. Мок-заголовки (sim/wasm/mocks/)
// подменяют Arduino/GxEPD2/SPI при компиляции.
//
// JavaScript загружает WASM-модуль, вызывает set_*() для заполнения
// данных, затем render_normal() или render_error(), и читает
// framebuffer через get_framebuffer().
// ================================================================

#include <emscripten/emscripten.h>

#include "display_renderer.h"
#include "weather_api.h"
#include "config.h"
#include "time_utils.h"

// ── Глобальные объекты, которые ожидает прошивка ────────────────
HardwareSerial Serial;
SPIClass SPI;
EspClass ESP;

// ── Состояние симулятора ────────────────────────────────────────
static Display display;
static WeatherData g_weather;
static int g_batteryPercent = 85;
static int g_wifiRSSI = -55;
static char g_ip[32] = "192.168.1.42";

// ── Dummy для линковки: fetchWeather() объявлена в weather_api.h,
//    но weather_api.cpp не компилируется для WASM ────────────────
WeatherData fetchWeather() { return WeatherData(); }

// ── Экспортируемые функции (вызываются из JavaScript) ───────────
extern "C" {

// Инициализация дисплея (вызвать один раз после загрузки WASM)
EMSCRIPTEN_KEEPALIVE
void init() {
    displayInit(display);
}

// Указатель на 1bpp framebuffer (800x480 = 48000 байт)
EMSCRIPTEN_KEEPALIVE
uint8_t* get_framebuffer() {
    return display.buffer;
}

// Размер framebuffer в байтах
EMSCRIPTEN_KEEPALIVE
int get_framebuffer_size() {
    return Display::BUF_SIZE;
}

// Ширина дисплея в пикселях
EMSCRIPTEN_KEEPALIVE
int get_display_width() {
    return Display::WIDTH;
}

// Высота дисплея в пикселях
EMSCRIPTEN_KEEPALIVE
int get_display_height() {
    return Display::HEIGHT;
}

// Установить данные текущей погоды
EMSCRIPTEN_KEEPALIVE
void set_current(float temp, float feels_like, float temp_min, float temp_max,
                 int humidity, int pressure, float pop,
                 const char* description, const char* icon, int weather_id,
                 int clouds, long sunrise, long sunset,
                 float wind_speed, int wind_deg, long dt, const char* city_name) {
    auto& c = g_weather.current;
    c.temp = temp;
    c.feels_like = feels_like;
    c.temp_min = temp_min;
    c.temp_max = temp_max;
    c.humidity = humidity;
    c.pressure = pressure;
    c.pop = pop;
    c.description = description;
    c.icon = icon;
    c.weather_id = weather_id;
    c.clouds = clouds;
    c.sunrise = sunrise;
    c.sunset = sunset;
    c.wind_speed = wind_speed;
    c.wind_deg = wind_deg;
    c.dt = dt;
    c.city_name = city_name;
}

// Установить прогноз на один день (index 0..FORECAST_DAYS-1)
EMSCRIPTEN_KEEPALIVE
void set_forecast(int index, const char* day_name, float temp_min, float temp_max,
                  float pop, const char* icon, int weather_id) {
    if (index < 0 || index >= FORECAST_DAYS) return;
    auto& f = g_weather.forecast[index];
    f.day_name = day_name;
    f.temp_min = temp_min;
    f.temp_max = temp_max;
    f.pop = pop;
    f.icon = icon;
    f.weather_id = weather_id;
    f.valid = true;
}

// Установить точку графика (index 0..HOURLY_POINTS-1)
EMSCRIPTEN_KEEPALIVE
void set_chart(int index, long dt, float temp, float pop) {
    if (index < 0 || index >= HOURLY_POINTS) return;
    g_weather.chart[index].dt = dt;
    g_weather.chart[index].temp = temp;
    g_weather.chart[index].pop = pop;
}

// Установить метаданные и timezone
EMSCRIPTEN_KEEPALIVE
void set_meta(int forecast_count, int chart_count,
              int sunny_days, int sunny_total_days, int pressure_trend,
              long timezone_offset_sec) {
    g_weather.forecast_count = forecast_count;
    g_weather.chart_count = chart_count;
    g_weather.sunny_days = sunny_days;
    g_weather.sunny_total_days = sunny_total_days;
    g_weather.pressure_trend = pressure_trend;
    g_weather.timezone_offset_sec = timezone_offset_sec;
    g_weather.valid = true;
    g_weather.error = "";
    setUtcOffsetSec(timezone_offset_sec);
}

// Установить параметры рендера (батарея, WiFi, IP)
EMSCRIPTEN_KEEPALIVE
void set_render_params(int battery, int rssi, const char* ip) {
    g_batteryPercent = battery;
    g_wifiRSSI = rssi;
    strncpy(g_ip, ip, sizeof(g_ip) - 1);
    g_ip[sizeof(g_ip) - 1] = '\0';
}

// Отрисовать нормальный экран погоды
EMSCRIPTEN_KEEPALIVE
void render_normal() {
    display.clearBuffer();
    displayRender(display, g_weather, g_batteryPercent, g_wifiRSSI, g_ip);
}

// Отрисовать экран ошибки
EMSCRIPTEN_KEEPALIVE
void render_error(const char* msg, long lastDt, int sleepMin) {
    display.clearBuffer();
    displayShowError(display, msg, g_batteryPercent, g_wifiRSSI, g_ip, lastDt, sleepMin);
}

} // extern "C"
