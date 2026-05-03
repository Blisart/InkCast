#include "display_renderer.h"
#include "config.h"
#include "icons.h"
#include "weather_icons.h"
#include "time_utils.h"
#include <U8g2_for_Adafruit_GFX.h>
#include "rubik_fonts_u8g2.h"

U8G2_FOR_ADAFRUIT_GFX u8g2;

// Шрифты Rubik (кириллица + ° + ASCII)
#define FONT_LARGE    rubik_u8g2_42            // 42px — температура
#define FONT_MEDIUM   rubik_u8g2_24            // 24px — тулбар, осадки, время
#define FONT_SMALL    rubik_u8g2_20            // 20px — текст, прогноз
#define FONT_SMALL_CYR rubik_u8g2_14           // 14px — описание, мелкий текст
#define FONT_CHART    u8g2_font_7x14_tf        // 14px — метки осей графика (ASCII)

// ═══════════════════════════════════════════
// Layout: 4 зоны, высоты → автоматические Y
// ═══════════════════════════════════════════
static constexpr int W = DISPLAY_WIDTH;              // 800
static constexpr int H = DISPLAY_HEIGHT;             // 480

static constexpr int STATUS_H   = 48;               // 10% — статус-бар
static constexpr int CURRENT_H  = 120;              // 25% — текущая погода
static constexpr int REMAINING  = H - STATUS_H - CURRENT_H; // 312
static constexpr int CHART_H    = REMAINING / 2;    // 156 — график
static constexpr int FORECAST_H = REMAINING - CHART_H; // 156 — прогноз

static constexpr int STATUS_Y   = 0;                // 0
static constexpr int CURRENT_Y  = STATUS_H;         // 48
static constexpr int CHART_Y    = CURRENT_Y + CURRENT_H;   // 168
static constexpr int FORECAST_Y = CHART_Y + CHART_H;       // 324

// ═══════════════════════════════════════════
// Вспомогательные функции отрисовки
// ═══════════════════════════════════════════

static void drawCentered(const char* str, int x, int y) {
    int16_t w = u8g2.getUTF8Width(str);
    u8g2.drawUTF8(x - w / 2, y, str);
}

static void drawDottedHLine(Display& display, int x1, int x2, int y, int step = 4) {
    for (int x = x1; x < x2; x += step) {
        display.drawPixel(x, y, GxEPD_BLACK);
    }
}

static void drawDottedVLine(Display& display, int x, int y1, int y2, int step = 4) {
    for (int y = y1; y < y2; y += step) {
        display.drawPixel(x, y, GxEPD_BLACK);
    }
}

static int wifiLevel(int rssi) {
    if (rssi > -50) return 3;
    if (rssi > -65) return 2;
    if (rssi > -80) return 1;
    return 0;
}

static int batteryLevel(int percent) {
    if (percent > 80) return 4;
    if (percent > 60) return 3;
    if (percent > 40) return 2;
    if (percent > 15) return 1;
    return 0;
}

static const unsigned char* getWifiIcon(int level) {
    switch (level) {
        case 3: return icon16_wifi_3;
        case 2: return icon16_wifi_2;
        case 1: return icon16_wifi_1;
        default: return icon16_wifi_0;
    }
}

static const unsigned char* getBatteryIcon(int level) {
    switch (level) {
        case 4: return icon16_battery_4;
        case 3: return icon16_battery_3;
        case 2: return icon16_battery_2;
        case 1: return icon16_battery_1;
        default: return icon16_battery_0;
    }
}

// ═══════════════════════════════════════════
// Зона 1: Статус-бар (48px)
// ═══════════════════════════════════════════

// Короткий маркер причины ошибки для статус-бара (4-6 символов).
// Маппит подстроки из weather.error / handleUpdateFailure-reason в понятные
// русские коды. Расположен рядом с индикатором ⊘ — дает пользователю понять,
// что именно сломалось, без перехода на полноэкранную карточку.
static String classifyErrorShort(const char* reason) {
    if (!reason || reason[0] == '\0') return "";
    String r(reason);
    if (r.indexOf("WiFi") >= 0)            return "сеть";
    if (r.indexOf("HTTP 401") >= 0)        return "ключ";
    if (r.indexOf("HTTP 429") >= 0)        return "лимит";
    if (r.indexOf("HTTP 5") >= 0)          return "сервер";
    if (r.indexOf("HTTP") >= 0)            return "HTTP";
    if (r.indexOf("JSON") >= 0)            return "JSON";
    if (r.indexOf("соединения") >= 0)      return "связь";
    return "API";
}

static void drawStatusBar(Display& display, const CurrentWeather& current,
                          int batteryPercent, int wifiRSSI, const char* ip,
                          bool fetchFailed = false,
                          const char* errorCode = nullptr) {
    int y0 = STATUS_Y;
    // Основной ряд опущен на 3px относительно верхней границы зоны,
    // чтобы IP-адрес сверху не "прилипал" к иконкам WiFi/батареи.
    int textY = y0 + 37;
    int iconY = y0 + 19;

    u8g2.setFont(FONT_MEDIUM);
    u8g2.drawUTF8(8, textY, current.city_name.c_str());

    // Дата основным шрифтом + время мелким (вторичная инфа), оба по baseline = textY,
    // совместно отцентрованы по горизонтали относительно W/2
    String dateStr = formatDate(current.dt);     // "Ср 26.03.2025"
    String timeStr = formatTime(current.dt);     // "14:30"
    const int dateTimeGap = 6;
    u8g2.setFont(FONT_MEDIUM);
    int dateW = u8g2.getUTF8Width(dateStr.c_str());
    u8g2.setFont(FONT_SMALL_CYR);
    int timeW = u8g2.getUTF8Width(timeStr.c_str());
    int dtStartX = (W - (dateW + dateTimeGap + timeW)) / 2;
    u8g2.setFont(FONT_MEDIUM);
    u8g2.drawUTF8(dtStartX, textY, dateStr.c_str());
    u8g2.setFont(FONT_SMALL_CYR);
    u8g2.drawUTF8(dtStartX + dateW + dateTimeGap, textY, timeStr.c_str());

    // Индикатор "данные устарели" слева от иконок WiFi/battery: жирный
    // контурный круг диаметром ~23px (3 концентрических кольца = толщина 3px)
    // с чёрным восклицательным знаком внутри.
    // Центр круга — та же вертикальная середина, что у иконок 16×16 (cy=iconY+8).
    if (fetchFailed) {
        int cx = W - 108;
        int cy = iconY + 8;
        display.drawCircle(cx, cy, 11, GxEPD_BLACK);
        display.drawCircle(cx, cy, 10, GxEPD_BLACK);
        display.drawCircle(cx, cy, 9,  GxEPD_BLACK);
        // Чёрный восклицательный знак: палочка 3×8 + точка 3×3 с gap 1px
        display.fillRect(cx - 1, cy - 6, 3, 8, GxEPD_BLACK);   // палочка
        display.fillRect(cx - 1, cy + 3, 3, 3, GxEPD_BLACK);   // точка

        // Короткий код причины слева от ⊘ (левый край индикатора = cx-11 = W-119),
        // с зазором 5px → правый край текста на W-124. Baseline = textY, чтобы
        // сидел на одной линии с городом/датой/батареей.
        if (errorCode && errorCode[0] != '\0') {
            u8g2.setFont(FONT_SMALL_CYR);
            int codeW = u8g2.getUTF8Width(errorCode);
            u8g2.drawUTF8(cx - 11 - 5 - codeW, textY, errorCode);
        }
    }

    display.drawBitmap(W - 90, iconY, getWifiIcon(wifiLevel(wifiRSSI)), 16, 16, GxEPD_BLACK);
    display.drawBitmap(W - 66, iconY, getBatteryIcon(batteryLevel(batteryPercent)), 16, 16, GxEPD_BLACK);
    u8g2.setFont(FONT_MEDIUM);
    String batStr = String(batteryPercent) + "%";
    u8g2.drawUTF8(W - 48, textY, batStr.c_str());

    // IP-адрес мелким шрифтом справа сверху, над иконками WiFi/батареи
    if (ip && ip[0] != '\0') {
        u8g2.setFont(FONT_SMALL_CYR);
        int ipW = u8g2.getUTF8Width(ip);
        u8g2.drawUTF8(W - 8 - ipW, y0 + 12, ip);
    }

    display.drawLine(0, STATUS_H, W, STATUS_H, GxEPD_BLACK);
}

// ═══════════════════════════════════════════
// Зона 2: Текущая погода (120px)
// ═══════════════════════════════════════════

static void drawCurrentWeather(Display& display, const WeatherData& weather) {
    int y0 = CURRENT_Y;

    // Иконка 96×96
    const unsigned char* curIcon = getWeatherIcon96(weather.current.weather_id, weather.current.icon);
    if (curIcon) {
        display.drawBitmap(8, y0 + 12, curIcon, 96, 96, GxEPD_BLACK);
    }

    // Температура: число крупно, °/min° мельче рядом
    int tempX = 116;
    u8g2.setFont(FONT_LARGE);
    String mainTemp = String((int)round(weather.current.temp)) + "\xC2\xB0";
    int mainW = u8g2.getUTF8Width(mainTemp.c_str());
    u8g2.drawUTF8(tempX, y0 + 48, mainTemp.c_str());

    u8g2.setFont(FONT_SMALL);
    String subTemp = "/" + String((int)round(weather.current.temp_min)) + "\xC2\xB0";
    u8g2.drawUTF8(tempX + mainW + 2, y0 + 48, subTemp.c_str());

    // Ощущается + максимум за день
    String infoStr = "ощ. " + String((int)round(weather.current.feels_like)) + "\xC2\xB0"
                   + "  макс " + String((int)round(weather.current.temp_max)) + "\xC2\xB0";
    u8g2.drawUTF8(tempX, y0 + 75, infoStr.c_str());

    // Описание погоды — мелким шрифтом с кириллицей
    u8g2.setFont(FONT_SMALL_CYR);
    u8g2.drawUTF8(tempX, y0 + 100, weather.current.description.c_str());

    // --- Правая часть: 3 колонки × 3 ряда ---
    int col1   = 350;                 // колонка 1: осадки, восход, закат
    int col2   = col1 + 130;         // колонка 2: трекер, давление, влажность
    int col3   = col2 + 140;         // колонка 3: ветер
    int val1   = col1 + 30;          // текст в кол.1 (после иконки 24px + 6px)
    int row0   = y0 + 14;
    int row1   = y0 + 48;
    int row2   = y0 + 82;
    int baseY0 = y0 + 36;
    int baseY1 = y0 + 69;
    int baseY2 = y0 + 102;

    // --- Колонка 1: осадки / восход / закат ---

    display.drawBitmap(col1, row0, icon24_raindrop, 24, 24, GxEPD_BLACK);
    u8g2.setFont(FONT_MEDIUM);
    String popMain = String((int)(weather.current.pop * 100)) + "%";
    int popW = u8g2.getUTF8Width(popMain.c_str());
    u8g2.drawUTF8(val1, baseY0, popMain.c_str());
    u8g2.setFont(FONT_SMALL_CYR);
    u8g2.drawUTF8(val1 + popW + 4, baseY0, "/ 12\xD1\x87");

    display.drawBitmap(col1, row1, icon24_sunrise, 24, 24, GxEPD_BLACK);
    u8g2.setFont(FONT_MEDIUM);
    u8g2.drawUTF8(val1, baseY1, formatTime(weather.current.sunrise).c_str());

    display.drawBitmap(col1, row2, icon24_sunset, 24, 24, GxEPD_BLACK);
    u8g2.setFont(FONT_MEDIUM);
    u8g2.drawUTF8(val1, baseY2, formatTime(weather.current.sunset).c_str());

    // --- Колонка 2: трекер / давление / влажность ---

    display.drawBitmap(col2, row0 - 2, icon28_sun, 32, 28, GxEPD_BLACK);
    u8g2.setFont(FONT_MEDIUM);
    String sunnyMain = String(weather.sunny_days) + "/" + String(weather.sunny_total_days);
    u8g2.drawUTF8(col2 + 34, baseY0, sunnyMain.c_str());

    // Давление: стрелка + значение + гПа
    int px = col2;
    int arrowMid = baseY1 - 10;
    if (weather.pressure_trend > 0) {
        display.fillTriangle(px + 5, arrowMid - 5, px, arrowMid + 4, px + 10, arrowMid + 4, GxEPD_BLACK);
    } else if (weather.pressure_trend < 0) {
        display.fillTriangle(px, arrowMid - 4, px + 10, arrowMid - 4, px + 5, arrowMid + 5, GxEPD_BLACK);
    } else {
        display.fillRect(px, arrowMid - 1, 10, 2, GxEPD_BLACK);
    }
    u8g2.setFont(FONT_MEDIUM);
    String pressVal = String(weather.current.pressure);
    u8g2.drawUTF8(px + 14, baseY1, pressVal.c_str());
    int pressW = u8g2.getUTF8Width(pressVal.c_str());
    u8g2.setFont(FONT_SMALL_CYR);
    u8g2.drawUTF8(px + 14 + pressW + 4, baseY1, "\xD0\xB3\xD0\x9F\xD0\xB0");

    // Влажность
    u8g2.setFont(FONT_MEDIUM);
    String humVal = String(weather.current.humidity) + "%";
    u8g2.drawUTF8(col2, baseY2, humVal.c_str());
    int humW = u8g2.getUTF8Width(humVal.c_str());
    u8g2.setFont(FONT_SMALL_CYR);
    u8g2.drawUTF8(col2 + humW + 4, baseY2, "\xD0\xB2\xD0\xBB\xD0\xB0\xD0\xB6\xD0\xBD.");

    // --- Колонка 3: ветер (компас + скорость) ---
    int outerR = 30;
    int innerR = 23;
    int cx = col3 + outerR + 2;
    int cy = y0 + 61;
    int windInfoX = cx + outerR + 28;  // скорость справа от компаса

    // Внешний круг (толстый — 2px)
    display.drawCircle(cx, cy, outerR, GxEPD_BLACK);
    display.drawCircle(cx, cy, outerR - 1, GxEPD_BLACK);

    // Внутренний круг (тонкий)
    display.drawCircle(cx, cy, innerR, GxEPD_BLACK);

    // 4 засечки между кругами (С, В, Ю, З)
    for (int i = 0; i < 4; i++) {
        float a = i * 90.0f * DEG_TO_RAD;
        float sa = sinf(a), ca = cosf(a);
        int x1 = cx + (int)(sa * innerR);
        int y1 = cy - (int)(ca * innerR);
        int x2 = cx + (int)(sa * (outerR - 1));
        int y2 = cy - (int)(ca * (outerR - 1));
        display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
    }

    // Буквы направлений за внешним кругом
    u8g2.setFont(FONT_SMALL_CYR);
    int labelOff = outerR + 10;
    drawCentered("\xD0\xA1", cx, cy - labelOff + 4);               // С (Север) — сверху
    drawCentered("\xD0\xAE", cx, cy + labelOff + 4);               // Ю (Юг) — снизу
    u8g2.drawUTF8(cx + labelOff - 3, cy + 5, "\xD0\x92");         // В (Восток) — справа
    u8g2.drawUTF8(cx - labelOff - 5, cy + 5, "\xD0\x97");         // З (Запад) — слева

    // Стрелка-ромб (откуда дует ветер)
    float windRad = weather.current.wind_deg * DEG_TO_RAD;
    float sw = sinf(windRad), cw = cosf(windRad);
    int needleLen = innerR - 3;
    int halfW = 5;

    int tipX  = cx + (int)(sw * needleLen);
    int tipY  = cy - (int)(cw * needleLen);
    int tailX = cx - (int)(sw * needleLen);
    int tailY = cy + (int)(cw * needleLen);
    int sX1   = cx + (int)(cw * halfW);
    int sY1   = cy + (int)(sw * halfW);
    int sX2   = cx - (int)(cw * halfW);
    int sY2   = cy - (int)(sw * halfW);

    // Передняя часть (залитая)
    display.fillTriangle(tipX, tipY, sX1, sY1, sX2, sY2, GxEPD_BLACK);
    // Задняя часть (контур)
    display.drawTriangle(tailX, tailY, sX1, sY1, sX2, sY2, GxEPD_BLACK);

    // Центральный кружок
    display.fillCircle(cx, cy, 3, GxEPD_WHITE);
    display.drawCircle(cx, cy, 3, GxEPD_BLACK);

    // Скорость ветра — справа от компаса
    u8g2.setFont(FONT_MEDIUM);
    String windVal = String((int)round(weather.current.wind_speed));
    u8g2.drawUTF8(windInfoX, cy + 4, windVal.c_str());
    int windW = u8g2.getUTF8Width(windVal.c_str());
    u8g2.setFont(FONT_SMALL_CYR);
    u8g2.drawUTF8(windInfoX + windW + 4, cy + 4, "\xD0\xBC/\xD1\x81");

    display.drawLine(0, CHART_Y, W, CHART_Y, GxEPD_BLACK);
}

// ═══════════════════════════════════════════
// Зона 3: График температуры + осадков (156px)
// ═══════════════════════════════════════════

// Параметры шкалы графика
struct ChartScale {
    int cX, cY, cW, cH;       // область рисования
    float tMin, tMax, tRange;  // диапазон температур
    int slotW;                 // ширина одного слота (точки)
    int count;
};

static ChartScale computeChartScale(const ChartPoint chart[], int count) {
    ChartScale s;
    s.count = count;

    int padL = 44, padR = 36, padT = 8, padB = 18;
    s.cX = padL;
    s.cY = CHART_Y + padT;
    s.cW = W - padL - padR;
    s.cH = CHART_H - padT - padB;
    s.slotW = s.cW / max(count, 1);

    s.tMin = 999; s.tMax = -999;
    for (int i = 0; i < count; i++) {
        if (chart[i].temp < s.tMin) s.tMin = chart[i].temp;
        if (chart[i].temp > s.tMax) s.tMax = chart[i].temp;
    }
    s.tMin = floor(s.tMin) - 2;
    s.tMax = ceil(s.tMax) + 2;
    s.tRange = s.tMax - s.tMin;
    if (s.tRange < 6) {
        float mid = (s.tMin + s.tMax) / 2;
        s.tMin = mid - 3;
        s.tMax = mid + 3;
        s.tRange = 6;
    }
    return s;
}

// X/Y координаты точки данных
static int chartPointX(const ChartScale& s, int i) { return s.cX + i * s.slotW + s.slotW / 2; }
static int chartPointY(const ChartScale& s, float temp) {
    return s.cY + s.cH - 1 - (int)((temp - s.tMin) / s.tRange * (s.cH - 2));
}

static void drawChartGrid(Display& display, const ChartScale& s) {
    // Рамка
    display.drawRect(s.cX, s.cY, s.cW, s.cH, GxEPD_BLACK);

    // Горизонтальная сетка + метки °C слева
    int gridStep = (s.tRange <= 12) ? 2 : 5;
    int gridStart = (int)(ceil(s.tMin / gridStep) * gridStep);
    u8g2.setFont(FONT_CHART);
    for (int t = gridStart; t < (int)s.tMax; t += gridStep) {
        int gy = chartPointY(s, t);
        if (gy > s.cY + 4 && gy < s.cY + s.cH - 4) {
            drawDottedHLine(display, s.cX + 1, s.cX + s.cW - 1, gy);
            char label[8];
            snprintf(label, sizeof(label), "%d°", t);
            int lw = u8g2.getUTF8Width(label);
            u8g2.drawUTF8(s.cX - lw - 4, gy + 4, label);
        }
    }

    // Метки % справа
    u8g2.drawUTF8(s.cX + s.cW + 4, s.cY + s.cH, "0%");
    u8g2.drawUTF8(s.cX + s.cW + 4, s.cY + s.cH / 2 + 4, "50");
    u8g2.drawUTF8(s.cX + s.cW + 4, s.cY + 8, "100%");
}

static void drawPrecipBars(Display& display, const ChartScale& s, const ChartPoint chart[]) {
    int barW = max(2, s.slotW - 4);
    for (int i = 0; i < s.count; i++) {
        int bx = s.cX + i * s.slotW + (s.slotW - barW) / 2;
        int barH = (int)(chart[i].pop * (s.cH - 2));
        if (barH > 1) {
            int barY = s.cY + s.cH - 1 - barH;
            for (int sx = bx; sx < bx + barW; sx += 2) {
                display.drawLine(sx, barY, sx, s.cY + s.cH - 2, GxEPD_BLACK);
            }
        }
    }
}

static void drawTempLine(Display& display, const ChartScale& s, const ChartPoint chart[]) {
    // Линия (2px)
    for (int i = 0; i < s.count - 1; i++) {
        int x1 = chartPointX(s, i),     y1 = chartPointY(s, chart[i].temp);
        int x2 = chartPointX(s, i + 1), y2 = chartPointY(s, chart[i + 1].temp);
        display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
        display.drawLine(x1, y1 - 1, x2, y2 - 1, GxEPD_BLACK);
    }

    // Подписи min/max
    int minIdx = 0, maxIdx = 0;
    for (int i = 1; i < s.count; i++) {
        if (chart[i].temp < chart[minIdx].temp) minIdx = i;
        if (chart[i].temp > chart[maxIdx].temp) maxIdx = i;
    }
    u8g2.setFont(FONT_CHART);
    char minLabel[8], maxLabel[8];
    snprintf(minLabel, sizeof(minLabel), "%d°", (int)round(chart[minIdx].temp));
    snprintf(maxLabel, sizeof(maxLabel), "%d°", (int)round(chart[maxIdx].temp));
    int miny = chartPointY(s, chart[minIdx].temp);
    int maxy = chartPointY(s, chart[maxIdx].temp);
    u8g2.drawUTF8(chartPointX(s, minIdx) - 8, min(miny + 14, s.cY + s.cH - 2), minLabel);
    u8g2.drawUTF8(chartPointX(s, maxIdx) - 8, max(maxy - 4, s.cY + 10), maxLabel);
}

// Синусоида положения солнца: ночная "чашка" заливается точечным паттерном
static void drawSunCurve(Display& display, const ChartScale& s, const ChartPoint chart[], long sunrise, long sunset) {
    if (s.count < 2) return;

    // Солнечный полдень — середина между восходом и закатом
    struct tm sr = localTm(sunrise);
    struct tm ss = localTm(sunset);
    float solarNoon = (sr.tm_hour + sr.tm_min / 60.0f + ss.tm_hour + ss.tm_min / 60.0f) / 2.0f;

    int amp = s.cH * 35 / 100;  // амплитуда: 35% высоты графика
    int midY = s.cY + s.cH / 2;

    int prevX = -1, prevSunY = -1;
    int prevMday = -1;

    for (int i = 0; i < s.count; i++) {
        struct tm tm = localTm(chart[i].dt);
        float h = tm.tm_hour + tm.tm_min / 60.0f;

        // +1 в солнечный полдень, -1 в полночь
        float phase = fmodf(h - solarNoon + 6.0f + 24.0f, 24.0f);
        float alt = sinf(2.0f * M_PI * phase / 24.0f);
        int sunY = midY - (int)(alt * amp);
        int curX = chartPointX(s, i);

        // Заливка ночной "чашки" (alt < 0)
        if (alt < 0.0f) {
            int x1 = max(s.cX + i * s.slotW, s.cX + 1);
            int x2 = min(s.cX + (i + 1) * s.slotW, s.cX + s.cW - 1);
            // Точки привязаны к глобальной сетке — без смещений между слотами
            int gridStep = 6;
            int yStart = s.cY + gridStep - (s.cY % gridStep);  // выровнять по глобальной сетке
            for (int y = yStart; y < s.cY + s.cH - 1; y += gridStep) {
                if (y < sunY) continue;  // выше синусоиды — не заливаем
                for (int x = s.cX + gridStep - (s.cX % gridStep); x < x2; x += gridStep) {
                    if (x < x1) continue;
                    display.drawPixel(x, y, GxEPD_BLACK);
                    display.drawPixel(x + 1, y, GxEPD_BLACK);
                    display.drawPixel(x, y + 1, GxEPD_BLACK);
                }
            }
        }

        // Линия синусоиды (пунктирная 2px)
        if (prevX >= 0) {
            for (int px = prevX; px <= curX; px += 4) {
                float t = (curX == prevX) ? 0 : (float)(px - prevX) / (curX - prevX);
                int py = prevSunY + (int)(t * (sunY - prevSunY));
                display.drawPixel(px, py, GxEPD_BLACK);
                display.drawPixel(px, py + 1, GxEPD_BLACK);
            }
        }
        prevX = curX;
        prevSunY = sunY;

        // Полночная линия + название дня
        if (prevMday >= 0 && tm.tm_mday != prevMday) {
            drawDottedVLine(display, curX, s.cY + 1, s.cY + s.cH - 2, 3);
            u8g2.setFont(FONT_SMALL_CYR);
            String dn = dayNameFromTimestamp(chart[i].dt);
            u8g2.drawUTF8(curX + 4, s.cY + 14, dn.c_str());
        }
        prevMday = tm.tm_mday;
    }
}

static void drawTimeLabels(Display& display, const ChartScale& s, const ChartPoint chart[]) {
    u8g2.setFont(FONT_CHART);
    for (int i = 0; i < s.count; i++) {
        struct tm tm = localTm(chart[i].dt);
        // Пропускаем полночь — она уже отрисована в drawDayBands
        if (tm.tm_hour == 0) continue;
        if (tm.tm_hour % 6 == 0) {
            int lx = chartPointX(s, i);
            drawDottedVLine(display, lx, s.cY + 1, s.cY + s.cH - 1);
            char timeBuf[6];
            snprintf(timeBuf, sizeof(timeBuf), "%02d", tm.tm_hour);
            int tw = u8g2.getUTF8Width(timeBuf);
            u8g2.drawUTF8(lx - tw / 2, s.cY + s.cH + 14, timeBuf);
        }
    }
}

static void drawChart(Display& display, const ChartPoint chart[], int count, long sunrise, long sunset) {
    if (count < 2) return;

    ChartScale s = computeChartScale(chart, count);
    drawSunCurve(display, s, chart, sunrise, sunset);  // ночной фон (первый слой)
    drawChartGrid(display, s);
    drawPrecipBars(display, s, chart);
    drawTempLine(display, s, chart);
    drawTimeLabels(display, s, chart);

    display.drawLine(0, FORECAST_Y, W, FORECAST_Y, GxEPD_BLACK);
}

// ═══════════════════════════════════════════
// Зона 4: Прогноз на 6 дней (156px)
// ═══════════════════════════════════════════

static void drawForecast(Display& display, const DayForecast forecast[], int count) {
    int y0 = FORECAST_Y + 4;
    int colW = W / FORECAST_DAYS;

    for (int i = 0; i < FORECAST_DAYS; i++) {
        int cx = colW * i + colW / 2;

        if (i < count && forecast[i].valid) {
            // День недели
            u8g2.setFont(FONT_SMALL);
            drawCentered(forecast[i].day_name.c_str(), cx, y0 + 20);

            // Иконка 64×64
            const unsigned char* fIcon = getWeatherIcon64(forecast[i].weather_id, forecast[i].icon);
            if (fIcon) {
                display.drawBitmap(cx - 32, y0 + 26, fIcon, 64, 64, GxEPD_BLACK);
            }

            // Температура: макс крупно, /мин мельче
            u8g2.setFont(FONT_MEDIUM);
            String tMax = String((int)round(forecast[i].temp_max));
            int tMaxW = u8g2.getUTF8Width(tMax.c_str());

            u8g2.setFont(FONT_SMALL);
            String tMin = "/" + String((int)round(forecast[i].temp_min)) + "\xC2\xB0";
            int tMinW = u8g2.getUTF8Width(tMin.c_str());

            int totalW = tMaxW + tMinW;
            int startX = cx - totalW / 2;

            u8g2.setFont(FONT_MEDIUM);
            u8g2.drawUTF8(startX, y0 + 118, tMax.c_str());
            u8g2.setFont(FONT_SMALL);
            u8g2.drawUTF8(startX + tMaxW, y0 + 118, tMin.c_str());

            // Осадки
            u8g2.setFont(FONT_SMALL);
            String fPopStr = String((int)(forecast[i].pop * 100)) + "%";
            drawCentered(fPopStr.c_str(), cx, y0 + 144);
        }

        if (i < FORECAST_DAYS - 1) {
            display.drawLine(colW * (i + 1), FORECAST_Y, colW * (i + 1), H, GxEPD_BLACK);
        }
    }
}

// ═══════════════════════════════════════════
// Публичные функции
// ═══════════════════════════════════════════

void displayInit(Display& display, bool initial) {
    SPI.begin(EPD_CLK, -1, EPD_MOSI, EPD_CS);
    display.init(115200, initial, 50, false);
    display.setRotation(2);
    display.setTextColor(GxEPD_BLACK);
    display.setTextWrap(false);
    u8g2.begin(display);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);
}

static void resolveErrorContent(const char* msg, String& title, String& detail, String& hint) {
    String text(msg);

    if (text.indexOf("WiFi") >= 0) {
        title  = "Нет сети";
        detail = "Не удалось подключиться к Wi-Fi";
        hint   = "Проверь SSID, пароль и уровень сигнала";
        return;
    }

    if (text.indexOf("Weather API") >= 0) {
        title = "Нет данных погоды";

        if (text.indexOf("HTTP 401") >= 0) {
            detail = "Ключ API не авторизован";
            hint   = "Подпишись на One Call 3.0 на openweathermap.org";
        } else if (text.indexOf("HTTP 429") >= 0) {
            detail = "Превышен лимит запросов";
            hint   = "Подожди или проверь план подписки";
        } else if (text.indexOf("HTTP") >= 0) {
            // Извлекаем всё после "Weather API: "
            int pos = text.indexOf(": ");
            detail = (pos >= 0) ? text.substring(pos + 2) : "Сервер вернул ошибку";
            hint   = "Проверь ключ и подписку One Call 3.0";
        } else {
            detail = "OpenWeather не отдал прогноз";
            hint   = "Проверь ключ, подписку и соединение";
        }
        return;
    }

    title  = "Ошибка обновления";
    detail = text;
    hint   = "Устройство попробует снова позже";
}

void displayShowError(Display& display, const char* msg, int batteryPercent, int wifiRSSI, const char* ip,
                      long lastDt, int sleepDurationMin) {
    String title;
    String detail;
    String hint;
    resolveErrorContent(msg, title, detail, hint);

    const int cardX = 72;
    const int cardY = STATUS_H + 40;
    const int cardW = W - cardX * 2;
    const int cardH = 260;

    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Статус-бар: "Обновление не удалось" + WiFi + батарея
        int textY = 32;
        u8g2.setFont(FONT_SMALL);
        u8g2.drawUTF8(8, textY, "Обновление не удалось");

        display.drawBitmap(W - 80, 16, getWifiIcon(wifiLevel(wifiRSSI)), 16, 16, GxEPD_BLACK);
        display.drawBitmap(W - 56, 16, getBatteryIcon(batteryLevel(batteryPercent)), 16, 16, GxEPD_BLACK);
        String batStr = String(batteryPercent) + "%";
        u8g2.drawUTF8(W - 38, textY, batStr.c_str());

        // Время последнего успешного обновления (через localTm, как везде в прошивке)
        if (lastDt > 0) {
            struct tm tm = localTm(lastDt);
            char timeBuf[20];
            snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d  %02d.%02d",
                     tm.tm_hour, tm.tm_min,
                     tm.tm_mday, tm.tm_mon + 1);
            drawCentered(timeBuf, W / 2, textY);
        }

        display.drawLine(0, STATUS_H, W, STATUS_H, GxEPD_BLACK);

        const bool hasIP = (ip && ip[0] != '\0');
        display.drawRoundRect(cardX, cardY, cardW, hasIP ? cardH + 30 : cardH, 10, GxEPD_BLACK);
        display.drawRoundRect(cardX + 8, cardY + 8, cardW - 16, (hasIP ? cardH + 30 : cardH) - 16, 8, GxEPD_BLACK);

        display.fillCircle(cardX + 54, cardY + 52, 18, GxEPD_BLACK);
        display.fillCircle(cardX + 54, cardY + 52, 13, GxEPD_WHITE);
        display.fillCircle(cardX + 112, cardY + 52, 18, GxEPD_BLACK);
        display.fillCircle(cardX + 112, cardY + 52, 13, GxEPD_WHITE);
        display.drawLine(cardX + 54, cardY + 86, cardX + 112, cardY + 86, GxEPD_BLACK);

        u8g2.setFont(FONT_MEDIUM);
        u8g2.drawUTF8(cardX + 150, cardY + 60, title.c_str());

        display.drawLine(cardX + 24, cardY + 108, cardX + cardW - 24, cardY + 108, GxEPD_BLACK);

        u8g2.setFont(FONT_SMALL);
        u8g2.drawUTF8(cardX + 28, cardY + 148, detail.c_str());
        u8g2.drawUTF8(cardX + 28, cardY + 184, hint.c_str());

        if (hasIP) {
            u8g2.setFont(FONT_SMALL);
            String ipStr = String("IP: ") + ip;
            u8g2.drawUTF8(cardX + 28, cardY + 220, ipStr.c_str());
        }

        String retry = "Повторная попытка через " + String(sleepDurationMin) + " мин";
        drawCentered(retry.c_str(), W / 2, cardY + (hasIP ? 260 : 230));

        u8g2.setFont(FONT_CHART);
        drawCentered("Последнее изображение на экране не обновлено", W / 2, H - 18);
    } while (display.nextPage());
    display.hibernate();
}

void displayRender(Display& display, const WeatherData& weather, int batteryPercent, int wifiRSSI, const char* ip) {
    display.setFullWindow();
    display.firstPage();

    do {
        display.fillScreen(GxEPD_WHITE);
        drawStatusBar(display, weather.current, batteryPercent, wifiRSSI, ip);
        drawCurrentWeather(display, weather);
        drawChart(display, weather.chart, weather.chart_count, weather.current.sunrise, weather.current.sunset);
        drawForecast(display, weather.forecast, weather.forecast_count);
    } while (display.nextPage());

    display.hibernate();
}

void displayUpdateStatusBarError(Display& display, long lastDt,
                                 int batteryPercent, int wifiRSSI, const char* ip,
                                 const char* cityName,
                                 const char* errorReason) {
    // Минимальный CurrentWeather только с тем, что нужно для статус-бара.
    CurrentWeather stub;
    stub.city_name = cityName;
    stub.dt = lastDt;

    // Преобразуем длинную техничную строку ошибки в короткий маркер ("сеть",
    // "ключ", "лимит", ...), который влезает рядом с индикатором ⊘.
    String shortCode = classifyErrorShort(errorReason);

    // setPartialWindow ограничивает обновление зоной статус-бара. GxEPD2 для
    // GDEM0397T81P (контроллер SSD2677) поддерживает partial — остальные зоны
    // остаются на стекле без перерисовки. Координаты по физической ширине,
    // не зависят от rotation (rotation=2 применяется внутри библиотеки).
    display.setPartialWindow(0, STATUS_Y, W, STATUS_H);

    // Flash-сброс против e-ink ghosting: при повторных partial update
    // чёрные пиксели (иконки, текст, индикатор ⊘) накапливают остаточный
    // заряд и со временем чернеют ещё сильнее. Принудительный цикл
    // чёрное → белое → контент прогоняет частицы через полную амплитуду
    // и стирает ghost только в зоне статус-бара — зоны 2-4 остаются нетронутыми.
    display.firstPage();
    do {
        display.fillRect(0, STATUS_Y, W, STATUS_H, GxEPD_BLACK);
    } while (display.nextPage());

    display.firstPage();
    do {
        display.fillRect(0, STATUS_Y, W, STATUS_H, GxEPD_WHITE);
    } while (display.nextPage());

    display.firstPage();
    do {
        display.fillRect(0, STATUS_Y, W, STATUS_H, GxEPD_WHITE);
        drawStatusBar(display, stub, batteryPercent, wifiRSSI, ip,
                      /*fetchFailed=*/true,
                      shortCode.length() ? shortCode.c_str() : nullptr);
    } while (display.nextPage());

    display.hibernate();
}
