#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

constexpr int FORECAST_DAYS  = 6;
constexpr int HOURLY_POINTS  = 48;

// Данные текущей погоды
struct CurrentWeather {
    float temp       = 0;      // °C
    float feels_like = 0;      // °C
    float temp_min   = 0;      // дневной минимум на сегодня, °C
    float temp_max   = 0;      // дневной максимум на сегодня, °C
    int   humidity   = 0;      // %
    int   pressure   = 0;      // hPa
    float pop        = 0;      // макс. вероятность осадков за ближайшие 12ч, 0..1
    String description;        // описание
    String icon;               // OWM icon code (01d, 02n, ...)
    int    weather_id = 0;     // OWM weather condition id (200, 500, 800, ...)
    int    clouds    = 0;      // облачность, % (0..100)
    long  sunrise    = 0;      // unix timestamp
    long  sunset     = 0;      // unix timestamp
    float wind_speed = 0;      // м/с
    int   wind_deg   = 0;      // направление ветра, градусы (0=С, 90=В, 180=Ю, 270=З)
    long  dt         = 0;      // unix timestamp обновления
    String city_name;          // название города для дисплея
};

// Данные прогноза на 1 день
struct DayForecast {
    String day_name;           // Пн, Вт, ...
    float  temp_min = 0;
    float  temp_max = 0;
    float  pop      = 0;      // вероятность осадков 0..1
    String icon;               // OWM icon code
    int    weather_id = 0;     // OWM weather condition id
    bool   valid    = false;
};

// Точка данных для графика (почасовой интервал из One Call 3.0)
struct ChartPoint {
    long  dt   = 0;            // unix timestamp
    float temp = 0;            // температура °C
    float pop  = 0;            // вероятность осадков 0..1
};

// Полный набор погодных данных
struct WeatherData {
    bool valid              = false;
    String error;           // причина ошибки (пустая при valid=true)
    CurrentWeather current;
    DayForecast forecast[FORECAST_DAYS];
    int forecast_count      = 0;
    ChartPoint chart[HOURLY_POINTS];
    int chart_count         = 0;
    int sunny_days          = 0;   // солнечных дней за период
    int sunny_total_days    = 0;   // из скольких дней считаем (макс 30)
    int pressure_trend      = 0;   // тренд давления: -1 падает, 0 стабильно, +1 растёт
    long timezone_offset_sec = 0;  // смещение от UTC в секундах для координат запроса (учитывает DST)
};

// Получить погоду (провайдер выбирается из g_settings.weatherProvider)
WeatherData fetchWeather();

// Результат тестирования подключения
struct ApiTestResult {
    int    httpCode = 0;   // HTTP-код ответа (200 = OK, 401 = плохой ключ, ...)
    String message;        // описание результата (для UI)
};

// Лёгкий тест подключения к погодному API.
// provider: 0 = OWM (apiKey обязателен), 1 = Open-Meteo (apiKey игнорируется).
ApiTestResult testApiConnection(const String& apiKey, const String& lat, const String& lon,
                                uint8_t provider = 0);
