#pragma once

#include <Arduino.h>

// Маппинг WMO weather codes (0-99) → OWM-совместимые weather_id и icon codes.
// Позволяет использовать существующие getWeatherIcon96/64() без изменений.

struct WmoMapped {
    uint16_t owmId;
    char     icon[4];  // "01d"/"01n" и т.д.
};

struct WmoEntry {
    uint8_t  code;
    uint16_t owmId;
    char     iconDay[4];
    char     iconNight[4];
    const char* descRu;
};

static const WmoEntry WMO_TABLE[] PROGMEM = {
    {  0, 800, "01d", "01n", "Ясно"                    },
    {  1, 800, "01d", "01n", "Преимущественно ясно"    },
    {  2, 802, "02d", "02n", "Переменная облачность"   },
    {  3, 804, "04d", "04n", "Облачно"                 },
    { 45, 741, "50d", "50n", "Туман"                   },
    { 48, 741, "50d", "50n", "Изморозь"                },
    { 51, 300, "09d", "09n", "Лёгкая морось"           },
    { 53, 301, "09d", "09n", "Морось"                  },
    { 55, 302, "09d", "09n", "Сильная морось"          },
    { 56, 511, "09d", "09n", "Ледяная морось"          },
    { 57, 511, "09d", "09n", "Сильная ледяная морось"  },
    { 61, 500, "10d", "10n", "Небольшой дождь"         },
    { 63, 501, "10d", "10n", "Дождь"                   },
    { 65, 502, "10d", "10n", "Сильный дождь"           },
    { 66, 511, "13d", "13n", "Ледяной дождь"           },
    { 67, 511, "13d", "13n", "Сильный ледяной дождь"   },
    { 71, 600, "13d", "13n", "Небольшой снег"          },
    { 73, 601, "13d", "13n", "Снег"                    },
    { 75, 602, "13d", "13n", "Сильный снег"            },
    { 77, 611, "13d", "13n", "Снежная крупа"           },
    { 80, 500, "09d", "09n", "Небольшой ливень"        },
    { 81, 501, "10d", "10n", "Ливень"                  },
    { 82, 502, "10d", "10n", "Сильный ливень"          },
    { 85, 620, "13d", "13n", "Небольшой снегопад"      },
    { 86, 621, "13d", "13n", "Сильный снегопад"        },
    { 95, 211, "11d", "11n", "Гроза"                   },
    { 96, 200, "11d", "11n", "Гроза с градом"          },
    { 99, 201, "11d", "11n", "Сильная гроза с градом"  },
};

static const int WMO_TABLE_SIZE = sizeof(WMO_TABLE) / sizeof(WMO_TABLE[0]);

// Маппинг WMO code → OWM-совместимые {owmId, icon}
inline WmoMapped mapWmoCondition(uint8_t code, bool isDay) {
    for (int i = 0; i < WMO_TABLE_SIZE; i++) {
        if (WMO_TABLE[i].code == code) {
            WmoMapped m;
            m.owmId = WMO_TABLE[i].owmId;
            memcpy(m.icon, isDay ? WMO_TABLE[i].iconDay : WMO_TABLE[i].iconNight, 4);
            return m;
        }
    }
    WmoMapped m;
    m.owmId = 804;
    memcpy(m.icon, isDay ? "04d" : "04n", 4);
    return m;
}

// WMO code → русское описание
inline const char* wmoDescriptionRu(uint8_t code) {
    for (int i = 0; i < WMO_TABLE_SIZE; i++) {
        if (WMO_TABLE[i].code == code) return WMO_TABLE[i].descRu;
    }
    return "Облачно";
}

// Open-Meteo weather_code 0..3 учитывает все облака целиком, включая высокие cirrus
// на 8+ км — из-за них при чистом небе ниже часто приходит code=3 ("Облачно").
// Для визуального восприятия важна только нижне-/среднеярусная облачность.
// Возвращает true и переписывает out/descRu, если код входит в диапазон 0..3.
inline bool refineCloudByLowMid(uint8_t code, int cloudLowMid, bool isDay,
                                WmoMapped& out, const char*& descRu) {
    if (code > 3) return false;
    if (cloudLowMid < 25) {
        out.owmId = 800;
        memcpy(out.icon, isDay ? "01d" : "01n", 4);
        descRu = "Ясно";
    } else if (cloudLowMid < 50) {
        out.owmId = 802;
        memcpy(out.icon, isDay ? "02d" : "02n", 4);
        descRu = "Переменная облачность";
    } else if (cloudLowMid < 85) {
        out.owmId = 803;
        memcpy(out.icon, isDay ? "03d" : "03n", 4);
        descRu = "Облачно с просветами";
    } else {
        out.owmId = 804;
        memcpy(out.icon, isDay ? "04d" : "04n", 4);
        descRu = "Облачно";
    }
    return true;
}
