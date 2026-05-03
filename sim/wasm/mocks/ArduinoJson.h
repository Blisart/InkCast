#pragma once
// Заглушка — ArduinoJson подключается через weather_api.h,
// но display_renderer.cpp не использует его типы напрямую.
// Структуры WeatherData/CurrentWeather и т.д. определены
// в weather_api.h без зависимости от ArduinoJson.
