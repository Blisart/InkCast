#pragma once

#include <GxEPD2_BW.h>
#include "weather_api.h"
#include "config.h"

// Тип дисплея GDEM0397T81P (3.97", 800×480, чёрно-белый, контроллер SSD2677)
typedef GxEPD2_BW<GxEPD2_397_GDEM0397T81, GxEPD2_397_GDEM0397T81::HEIGHT> Display;

// Инициализация дисплея.
// initial=true — полная инициализация (очистка экрана), initial=false —
// без очистки (сохраняет содержимое e-ink после deep sleep).
void displayInit(Display& display, bool initial = true);

// Отрисовка всего экрана
void displayRender(Display& display, const WeatherData& weather, int batteryPercent, int wifiRSSI, const char* ip);

// Показ ошибки на экране (кириллица поддерживается).
// lastDt — timestamp последнего успешного обновления (для отрисовки времени в статус-баре,
// 0 если обновлений ещё не было).
void displayShowError(Display& display, const char* msg, int batteryPercent, int wifiRSSI, const char* ip,
                      long lastDt = 0, int sleepDurationMin = SLEEP_DURATION_MIN);

// Partial update только статус-бара: добавить иконку ошибки рядом с WiFi/battery,
// сохранив остальные зоны (текущая погода / график / прогноз) в неизменном виде.
// Используется, когда последний fetch упал, но e-ink уже показывает данные
// предыдущего успешного цикла. lastDt — timestamp последнего успешного обновления
// (для отрисовки даты/времени в статус-баре, обычно из RTC_DATA_ATTR).
// errorReason — полное сообщение об ошибке (например "WiFi connection failed"
// или "Weather API: HTTP 401"). Преобразуется в короткий 4-6-символьный маркер
// рядом с индикатором ⊘ ("сеть", "ключ", "лимит", "HTTP", "JSON", "API").
// Если nullptr/пусто — маркер не рисуется (только индикатор).
void displayUpdateStatusBarError(Display& display, long lastDt,
                                 int batteryPercent, int wifiRSSI, const char* ip,
                                 const char* cityName,
                                 const char* errorReason = nullptr);
