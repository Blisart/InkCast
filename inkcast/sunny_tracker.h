#pragma once

#include <Preferences.h>
#include "time_utils.h"

// Трекер солнечных дней за последние 30 дней.
// Хранит битовую маску в NVS (переживает перезагрузки и deep sleep).
// Бит 0 = сегодня, бит 1 = вчера, ...
// День считается солнечным, если облачность < 50%.

inline int updateSunnyTracker(bool isSunnyToday, long timestamp, int& totalDays) {
    Preferences prefs;
    prefs.begin("weather", false);

    // Номер дня с начала эпохи (локальное время — runtime offset из time_utils.h)
    int today = (int)((timestamp + getUtcOffsetSec()) / 86400L);

    int lastDay       = prefs.getInt("sun_day", -1);
    uint32_t history  = prefs.getUInt("sun_bits", 0);
    totalDays         = prefs.getInt("sun_cnt", 0);

    if (lastDay >= 0) {
        int elapsed = today - lastDay;
        if (elapsed > 30 || elapsed < 0) {
            // Слишком давно или аномалия — сброс
            history = 0;
            totalDays = 0;
        } else if (elapsed > 0) {
            // Сдвигаем историю на количество прошедших дней
            history <<= elapsed;
            totalDays = min(totalDays + elapsed, 30);
        }
        // elapsed == 0: тот же день — обновляем бит 0
    } else {
        totalDays = 0;
    }

    // Сегодняшний день
    if (isSunnyToday) {
        history |= 1U;
    } else {
        history &= ~1U;
    }
    if (totalDays == 0) totalDays = 1;

    // Подсчёт солнечных дней (popcount нижних 30 бит)
    int count = 0;
    uint32_t bits = history & 0x3FFFFFFFU;
    while (bits) { count += bits & 1; bits >>= 1; }

    prefs.putUInt("sun_bits", history);
    prefs.putInt("sun_day", today);
    prefs.putInt("sun_cnt", totalDays);
    prefs.end();

    return count;
}
