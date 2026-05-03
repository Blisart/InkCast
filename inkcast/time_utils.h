#pragma once

#include <Arduino.h>
#include "config.h"

// Общие утилиты для работы с unix timestamp + смещение часового пояса + gmtime_r.
// Используются в weather_api.cpp и display_renderer.cpp.
//
// Часовой пояс — runtime: после успешного fetchWeather() из ответа OWM
// (поле "timezone_offset", секунды от UTC, уже учитывает DST) вызывается
// setUtcOffsetSec(...). До первого успешного запроса используется
// compile-time дефолт UTC_OFFSET_SEC из config.h.

static const char* TIME_DAY_NAMES[] = {"Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб"};

static const char* TIME_MONTH_NAMES[] = {
    "Янв", "Фев", "Мар", "Апр", "Май", "Июн",
    "Июл", "Авг", "Сен", "Окт", "Ноя", "Дек"
};

// Runtime-смещение часового пояса в секундах. Meyers singleton: локальная static
// внутри inline-функции — единое определение во всех TU без C++17 inline-vars и без .cpp.
inline long& utcOffsetSecRef() {
    static long offset = UTC_OFFSET_SEC;
    return offset;
}
inline long getUtcOffsetSec()       { return utcOffsetSecRef(); }
inline void setUtcOffsetSec(long s) { utcOffsetSecRef() = s; }

// Локальное время из unix timestamp (смещение runtime, см. setUtcOffsetSec)
inline struct tm localTm(long timestamp) {
    time_t t = timestamp + getUtcOffsetSec();
    struct tm tm_buf;
    gmtime_r(&t, &tm_buf);
    return tm_buf;
}

// "Пн", "Вт", ...
inline String dayNameFromTimestamp(long ts) {
    struct tm tm = localTm(ts);
    return TIME_DAY_NAMES[tm.tm_wday];
}

// "Ср 26.03.2025"
inline String formatDate(long timestamp) {
    struct tm tm = localTm(timestamp);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02d.%02d.%d", tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900);
    return String(TIME_DAY_NAMES[tm.tm_wday]) + " " + buf;
}

// "Ср 26.03.2025 14:30" — компактный формат для статус-бара,
// показывает время последнего обновления рядом с полной датой
inline String formatDateTimeCompact(long timestamp) {
    struct tm tm = localTm(timestamp);
    char buf[24];
    snprintf(buf, sizeof(buf), "%02d.%02d.%d %02d:%02d",
             tm.tm_mday, tm.tm_mon + 1, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
    return String(TIME_DAY_NAMES[tm.tm_wday]) + " " + buf;
}

// "14:30"
inline String formatTime(long timestamp) {
    struct tm tm = localTm(timestamp);
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
    return String(buf);
}
