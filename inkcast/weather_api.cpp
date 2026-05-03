#include "weather_api.h"
#include "open_meteo_map.h"
#include "config.h"
#include "settings.h"
#include "time_utils.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ═══════════════════════════════════════════
// Transport
// ═══════════════════════════════════════════

// ISRG Root X1 — корневой CA для Let's Encrypt (используется api.openweathermap.org).
// Срок действия: до 2035-06-04.
static const char ISRG_ROOT_X1[] PROGMEM = R"EOF(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6
UA5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+s
WT8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qy
HB5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4xIWw+5Tv/JHTwM9HH
vDN223LNb5np9s1u151pTQ/B10DYkTagplW1bY8yREUQuN4iUe1JkZ3SpssoeHw97
auM27qkdG6lnLIAaOsJo2bEqtAABUMGYOcj4WEpVpa2hJtPLsBFc+r6OE5qjVn2T/
S3AlR0cMHs0aX7NhB3mTqqMKwXPBMGogFqiMBPH/A50/BH1ELgvPs0tL4nRRhY2+
u0hprp4A/vMHKArVjGRGdaNqhILPnUk/xjnO3OU31sic6BAas3YNpLYMPHtwofCQ
g7T0MBV9u3rxOJnvN3f9d5su3/8Q8K0FNXnJGFh91DjOaRIL4DaQHoD1cPf3R8rr
PZbj5qoE4gRG9mJl2QP3SkfnAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogZiUvsY12QwGUxPBjkMeXNg1UZ8g/Z/cCQLgN1UNtted01
4GuS7KKaIxFM/TGLuMFhO1PjkPe/jF4r/5fvYj/N9cxb3kdhhmQxkuItiF5W9prT
tFqq5TpcOPlW3STU71xpVDdpMDTHA0lkX4/yGHRj7ok32CNW8WAS36rgpkCDsaXT
mDaBk3OuajgR/7vlk25EPNbAqXaPsPP5s1KYP+x/HCh0rVvYYGDtab9LkDnP+sFb
u09OYIG+Q5S34PZdMFnlj0ts5K0dlq1FJKR2CjKni2uogRSP8xJW+CnWHwNpXal6
HlqaCGqO/2Sa7rPMFoIT6h5bkVuga/hNo4CZ0IB5F9PYFBGo+NuBZhd8P3FBmSaB
k7IBjZPDI1NxPKCf+gQ2WSOV1rFPN0C9QKRH/8KHm+sHb5GtF0+/UoP1K9Bg7Iv
hVo+m/+EhpGeV/5v1oja/YGjaG1GBkT0OPruxvsJ+LfB4tQK8LOWfhYkdbqs=
-----END CERTIFICATE-----
)EOF";

// Координаты и API-ключ берутся из runtime настроек (NVS, см. settings.h),
// а units/lang остаются compile-time константами из config.h
static String buildOneCallUrl() {
    return "https://api.openweathermap.org/data/3.0/onecall?"
           "lat=" + g_settings.owmLat +
           "&lon=" + g_settings.owmLon +
           "&appid=" + g_settings.owmApiKey +
           "&units=" + String(OWM_UNITS) +
           "&lang=" + String(OWM_LANG) +
           "&exclude=minutely,alerts";
}

// Результат HTTP-запроса
struct HttpResult {
    int code = 0;
    String body;
};

// Извлекает hostname из URL ("https://host.com/path" → "host.com")
static String extractHost(const String& url) {
    int start = url.indexOf("://");
    if (start < 0) return "";
    start += 3;
    int end = url.indexOf('/', start);
    return (end < 0) ? url.substring(start) : url.substring(start, end);
}

static HttpResult httpGet(const String& url) {
    HttpResult result;

    Serial.printf("[HTTP] heap: %u free, %u max block\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());

    String host = extractHost(url);

    // DNS-проверка
    IPAddress resolved;
    if (WiFi.hostByName(host.c_str(), resolved)) {
        Serial.printf("[HTTP] DNS: %s -> %s\n", host.c_str(), resolved.toString().c_str());
    } else {
        Serial.printf("[HTTP] DNS resolve FAILED: %s\n", host.c_str());
    }

    WiFiClientSecure client;
    // OWM — валидация по ISRG Root X1; Open-Meteo (Cloudflare) — без пиннинга CA,
    // т.к. Cloudflare ротирует сертификаты. Open-Meteo — публичный API без секретов.
    if (host.indexOf("openweathermap") >= 0) {
        client.setCACert(ISRG_ROOT_X1);
    } else {
        client.setInsecure();
    }
    client.setTimeout(10);                 // секунды (read/write на сокете)
    client.setHandshakeTimeout(10);        // секунды (TLS handshake)

    HTTPClient http;
    http.setConnectTimeout(8000);          // мс — TCP connect
    http.setTimeout(10000);                // мс — общий HTTP read timeout

    Serial.println("[HTTP] connecting...");
    if (!http.begin(client, url)) {
        Serial.println("[HTTP] begin() failed");
        result.code = -1;
        return result;
    }

    Serial.println("[HTTP] sending GET");
    result.code = http.GET();
    Serial.printf("[HTTP] code=%d\n", result.code);
    if (result.code < 0) {
        Serial.printf("[HTTP] error: %s\n", http.errorToString(result.code).c_str());
    }

    if (result.code > 0) {
        result.body = http.getString();
    }

    if (result.code != HTTP_CODE_OK) {
        Serial.printf("[HTTP] GET failed, code: %d\n", result.code);
        if (!result.body.isEmpty()) {
            Serial.println("[HTTP] Response body:");
            Serial.println(result.body);
        }
    }
    http.end();
    return result;
}

// ═══════════════════════════════════════════
// Parse
// ═══════════════════════════════════════════

static void parseCurrent(JsonObject cur, JsonArray daily, JsonArray hourly, CurrentWeather& out) {
    out.temp        = cur["temp"].as<float>();
    out.feels_like  = cur["feels_like"].as<float>();
    out.humidity    = cur["humidity"].as<int>();
    out.pressure    = cur["pressure"].as<int>();
    JsonArray weather = cur["weather"];
    if (weather.size() > 0) {
        out.description = weather[0]["description"].as<String>();
        out.icon        = weather[0]["icon"].as<String>();
        out.weather_id  = weather[0]["id"].as<int>();
    }
    out.clouds      = cur["clouds"].as<int>();
    out.sunrise     = cur["sunrise"].as<long>();
    out.sunset      = cur["sunset"].as<long>();
    out.wind_speed  = cur["wind_speed"].as<float>();
    out.wind_deg    = cur["wind_deg"].as<int>();
    out.dt          = cur["dt"].as<long>();
    out.city_name   = g_settings.owmCity;

    // Дневной диапазон — из первого элемента daily
    if (!daily.isNull() && !daily[0].isNull()) {
        out.temp_min = daily[0]["temp"]["min"].as<float>();
        out.temp_max = daily[0]["temp"]["max"].as<float>();
    }

    // Макс. вероятность осадков за ближайшие 12 часов
    out.pop = 0;
    int hours = min((size_t)12, hourly.size());
    for (int i = 0; i < hours; i++) {
        float p = hourly[i]["pop"].as<float>();
        if (p > out.pop) out.pop = p;
    }
}

static void parseHourly(JsonArray hourly, ChartPoint chart[], int& chart_count) {
    chart_count = 0;
    for (JsonObject h : hourly) {
        if (chart_count >= HOURLY_POINTS) break;
        chart[chart_count].dt   = h["dt"].as<long>();
        chart[chart_count].temp = h["temp"].as<float>();
        chart[chart_count].pop  = h["pop"].as<float>();
        chart_count++;
    }
}

static void parseDaily(JsonArray daily, DayForecast forecast[], int& forecast_count) {
    forecast_count = 0;
    bool skipToday = true;
    for (JsonObject d : daily) {
        if (skipToday) { skipToday = false; continue; }
        if (forecast_count >= FORECAST_DAYS) break;

        int i = forecast_count;
        forecast[i].day_name = dayNameFromTimestamp(d["dt"].as<long>());
        forecast[i].temp_min = d["temp"]["min"].as<float>();
        forecast[i].temp_max = d["temp"]["max"].as<float>();
        forecast[i].pop      = d["pop"].as<float>();
        JsonArray dw = d["weather"];
        if (dw.size() > 0) {
            forecast[i].icon       = dw[0]["icon"].as<String>();
            forecast[i].weather_id = dw[0]["id"].as<int>();
        }
        forecast[i].valid    = true;
        forecast_count++;
    }
}

// ═══════════════════════════════════════════
// Derive
// ═══════════════════════════════════════════

static void computeDerivedFields(WeatherData& /* data */) {
    // sunny_days заполняется из NVS-трекера в .ino (30-дневная история)
}

// Проверяет наличие обязательных полей. Возвращает пустую строку при успехе, иначе — причину.
static String validateOneCallPayload(JsonDocument& doc) {
    JsonObject cur   = doc["current"];
    JsonArray hourly = doc["hourly"];
    JsonArray daily  = doc["daily"];

    if (cur.isNull() || hourly.isNull() || daily.isNull()) {
        // OWM при ошибке возвращает {cod: ..., message: ...}
        String msg = doc["message"].as<String>();
        int cod    = doc["cod"].as<int>();

        if (msg.length() > 0) {
            Serial.printf("[OWM] %d: %s\n", cod, msg.c_str());
            return "OWM: " + msg;
        }
        return "Ответ API не содержит данных";
    }

    if (hourly.size() == 0 || daily.size() == 0) {
        return "Пустой почасовой/дневной прогноз";
    }

    return "";  // ok
}

// ═══════════════════════════════════════════
// Open-Meteo
// ═══════════════════════════════════════════

static String buildOpenMeteoUrl() {
    String url = "https://api.open-meteo.com/v1/forecast?"
           "latitude=" + g_settings.owmLat +
           "&longitude=" + g_settings.owmLon +
           "&current=temperature_2m,apparent_temperature,relative_humidity_2m,"
           "pressure_msl,cloud_cover,wind_speed_10m,wind_direction_10m,"
           "weather_code,is_day"
           "&hourly=temperature_2m,precipitation_probability"
           "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,"
           "weather_code,sunrise,sunset"
           "&timeformat=unixtime"
           "&wind_speed_unit=ms"
           "&timezone=auto"
           "&forecast_days=7";
    if (g_settings.omModel.length() > 0 && g_settings.omModel != "best_match") {
        url += "&models=" + g_settings.omModel;
    }
    return url;
}

static void parseOmCurrent(JsonObject cur, JsonObject daily, CurrentWeather& out) {
    out.temp       = cur["temperature_2m"].as<float>();
    out.feels_like = cur["apparent_temperature"].as<float>();
    out.humidity   = cur["relative_humidity_2m"].as<int>();
    out.pressure   = (int)cur["pressure_msl"].as<float>();
    out.clouds     = cur["cloud_cover"].as<int>();
    out.wind_speed = cur["wind_speed_10m"].as<float>();
    out.wind_deg   = cur["wind_direction_10m"].as<int>();
    out.dt         = cur["time"].as<long>();
    out.city_name  = g_settings.owmCity;

    // WMO code → OWM-совместимые weather_id + icon + описание
    uint8_t wmo = cur["weather_code"].as<int>();
    bool isDay  = cur["is_day"].as<int>() == 1;
    WmoMapped m = mapWmoCondition(wmo, isDay);
    out.weather_id  = m.owmId;
    out.icon        = m.icon;
    out.description = wmoDescriptionRu(wmo);

    // temp_min/max из daily[0] (сегодня)
    JsonArray dMax = daily["temperature_2m_max"];
    JsonArray dMin = daily["temperature_2m_min"];
    if (dMax.size() > 0) out.temp_max = dMax[0].as<float>();
    if (dMin.size() > 0) out.temp_min = dMin[0].as<float>();

    // sunrise/sunset из daily[0]
    JsonArray sr = daily["sunrise"];
    JsonArray ss = daily["sunset"];
    if (sr.size() > 0) out.sunrise = sr[0].as<long>();
    if (ss.size() > 0) out.sunset  = ss[0].as<long>();

    // pop заполняется оркестратором после parseOmHourly из data.chart
    // (у Open-Meteo hourly начинается с 00:00 дня, а не с текущего часа как у OWM)
    out.pop = 0;
}

// currentDt — timestamp текущего часа из current block (для пропуска прошедших часов)
static void parseOmHourly(JsonObject hourly, ChartPoint chart[], int& chart_count, long currentDt) {
    chart_count = 0;
    JsonArray times = hourly["time"];
    JsonArray temps = hourly["temperature_2m"];
    JsonArray pops  = hourly["precipitation_probability"];

    size_t total = times.size();
    if (temps.size() < total) total = temps.size();

    // Open-Meteo hourly начинается с 00:00 сегодня (168 элементов = 7 дней).
    // OWM начинается с текущего часа. Пропускаем прошедшие, чтобы chart
    // показывал 48ч вперёд, как у OWM.
    size_t startIdx = 0;
    for (size_t i = 0; i < total; i++) {
        if (times[i].as<long>() >= currentDt) {
            startIdx = i;
            break;
        }
    }

    for (size_t i = startIdx; i < total && chart_count < HOURLY_POINTS; i++) {
        chart[chart_count].dt   = times[i].as<long>();
        chart[chart_count].temp = temps[i].as<float>();
        chart[chart_count].pop  = (i < pops.size()) ? pops[i].as<float>() / 100.0f : 0;
        chart_count++;
    }
}

static void parseOmDaily(JsonObject daily, DayForecast forecast[], int& forecast_count) {
    forecast_count = 0;
    JsonArray times = daily["time"];
    JsonArray tMax  = daily["temperature_2m_max"];
    JsonArray tMin  = daily["temperature_2m_min"];
    JsonArray pops  = daily["precipitation_probability_max"];
    JsonArray codes = daily["weather_code"];

    // skip index 0 (today), берём 1..6
    for (size_t i = 1; i < times.size() && forecast_count < FORECAST_DAYS; i++) {
        int fi = forecast_count;
        forecast[fi].day_name   = dayNameFromTimestamp(times[i].as<long>());
        forecast[fi].temp_max   = tMax[i].as<float>();
        forecast[fi].temp_min   = tMin[i].as<float>();
        forecast[fi].pop        = (pops.size() > i) ? pops[i].as<float>() / 100.0f : 0;

        uint8_t wmo = (codes.size() > i) ? codes[i].as<int>() : 3;
        WmoMapped m = mapWmoCondition(wmo, true);  // daily → дневная иконка
        forecast[fi].weather_id = m.owmId;
        forecast[fi].icon       = m.icon;
        forecast[fi].valid      = true;
        forecast_count++;
    }
}

static WeatherData fetchWeatherFromOm() {
    WeatherData data;

    Serial.println("[OM] Requesting Open-Meteo...");
    HttpResult resp = httpGet(buildOpenMeteoUrl());

    if (resp.code <= 0) {
        data.error = "Нет соединения с сервером Open-Meteo";
        return data;
    }
    if (resp.code != HTTP_CODE_OK) {
        data.error = "HTTP " + String(resp.code);
        JsonDocument errDoc;
        if (!deserializeJson(errDoc, resp.body)) {
            String reason = errDoc["reason"].as<String>();
            if (reason.length() > 0) data.error += ": " + reason;
        }
        return data;
    }

    // JSON-фильтр для экономии RAM
    JsonDocument filter;
    filter["utc_offset_seconds"] = true;
    filter["current"]["time"] = true;
    filter["current"]["temperature_2m"] = true;
    filter["current"]["apparent_temperature"] = true;
    filter["current"]["relative_humidity_2m"] = true;
    filter["current"]["pressure_msl"] = true;
    filter["current"]["cloud_cover"] = true;
    filter["current"]["wind_speed_10m"] = true;
    filter["current"]["wind_direction_10m"] = true;
    filter["current"]["weather_code"] = true;
    filter["current"]["is_day"] = true;
    filter["hourly"]["time"] = true;
    filter["hourly"]["temperature_2m"] = true;
    filter["hourly"]["precipitation_probability"] = true;
    filter["daily"]["time"] = true;
    filter["daily"]["temperature_2m_max"] = true;
    filter["daily"]["temperature_2m_min"] = true;
    filter["daily"]["precipitation_probability_max"] = true;
    filter["daily"]["weather_code"] = true;
    filter["daily"]["sunrise"] = true;
    filter["daily"]["sunset"] = true;
    // Open-Meteo ошибки
    filter["reason"] = true;
    filter["error"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp.body,
        DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[JSON] Ошибка парсинга Open-Meteo: %s\n", err.c_str());
        data.error = String("JSON: ") + err.c_str();
        return data;
    }

    // Проверка структуры
    if (doc["current"].isNull() || doc["hourly"].isNull() || doc["daily"].isNull()) {
        String reason = doc["reason"].as<String>();
        if (reason.length() > 0) {
            data.error = "Open-Meteo: " + reason;
        } else {
            data.error = "Ответ Open-Meteo не содержит данных";
        }
        return data;
    }

    // Timezone — приходит как число, сразу готово
    if (doc["utc_offset_seconds"].is<long>()) {
        data.timezone_offset_sec = doc["utc_offset_seconds"].as<long>();
        setUtcOffsetSec(data.timezone_offset_sec);
        Serial.printf("[OM] utc_offset_seconds: %ld\n", data.timezone_offset_sec);
    } else {
        data.timezone_offset_sec = getUtcOffsetSec();
    }

    JsonObject cur    = doc["current"];
    JsonObject hourly = doc["hourly"];
    JsonObject daily  = doc["daily"];

    parseOmCurrent(cur, daily, data.current);
    parseOmHourly(hourly, data.chart, data.chart_count, data.current.dt);
    parseOmDaily(daily, data.forecast, data.forecast_count);

    // Макс. вероятность осадков за ближайшие 12 часов — из chart,
    // который уже отфильтрован от прошедших часов.
    int hours = (data.chart_count < 12) ? data.chart_count : 12;
    for (int i = 0; i < hours; i++) {
        if (data.chart[i].pop > data.current.pop) data.current.pop = data.chart[i].pop;
    }

    computeDerivedFields(data);

    data.valid = true;
    return data;
}

// ═══════════════════════════════════════════
// OWM Orchestrator
// ═══════════════════════════════════════════

static WeatherData fetchWeatherFromOwm() {
    WeatherData data;

    Serial.println("[OWM] Requesting One Call 3.0...");
    HttpResult resp = httpGet(buildOneCallUrl());

    if (resp.code <= 0) {
        data.error = "Нет соединения с сервером";
        return data;
    }
    if (resp.code != HTTP_CODE_OK) {
        data.error = "HTTP " + String(resp.code);
        JsonDocument errDoc;
        if (!deserializeJson(errDoc, resp.body)) {
            String msg = errDoc["message"].as<String>();
            if (msg.length() > 0) data.error += ": " + msg;
        }
        return data;
    }

    JsonDocument filter;
    filter["current"]["temp"] = true;
    filter["current"]["feels_like"] = true;
    filter["current"]["humidity"] = true;
    filter["current"]["pressure"] = true;
    filter["current"]["weather"][0]["description"] = true;
    filter["current"]["weather"][0]["icon"] = true;
    filter["current"]["weather"][0]["id"] = true;
    filter["current"]["clouds"] = true;
    filter["current"]["sunrise"] = true;
    filter["current"]["sunset"] = true;
    filter["current"]["wind_speed"] = true;
    filter["current"]["wind_deg"] = true;
    filter["current"]["dt"] = true;
    filter["hourly"][0]["dt"] = true;
    filter["hourly"][0]["temp"] = true;
    filter["hourly"][0]["pop"] = true;
    filter["daily"][0]["dt"] = true;
    filter["daily"][0]["temp"]["min"] = true;
    filter["daily"][0]["temp"]["max"] = true;
    filter["daily"][0]["pop"] = true;
    filter["daily"][0]["weather"][0]["icon"] = true;
    filter["daily"][0]["weather"][0]["id"] = true;
    filter["timezone_offset"] = true;
    filter["cod"] = true;
    filter["message"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, resp.body,
        DeserializationOption::Filter(filter));
    if (err) {
        Serial.printf("[JSON] Ошибка парсинга: %s\n", err.c_str());
        data.error = String("JSON: ") + err.c_str();
        return data;
    }

    String validateErr = validateOneCallPayload(doc);
    if (!validateErr.isEmpty()) {
        data.error = validateErr;
        return data;
    }

    if (doc["timezone_offset"].is<long>()) {
        data.timezone_offset_sec = doc["timezone_offset"].as<long>();
        setUtcOffsetSec(data.timezone_offset_sec);
        Serial.printf("[OWM] timezone_offset: %ld сек\n", data.timezone_offset_sec);
    } else {
        data.timezone_offset_sec = getUtcOffsetSec();
        Serial.println("[OWM] timezone_offset отсутствует — использую дефолт config.h");
    }

    JsonObject cur     = doc["current"];
    JsonArray  hourly  = doc["hourly"];
    JsonArray  daily   = doc["daily"];

    parseCurrent(cur, daily, hourly, data.current);
    parseHourly(hourly, data.chart, data.chart_count);
    parseDaily(daily, data.forecast, data.forecast_count);
    computeDerivedFields(data);

    data.valid = true;
    return data;
}

// ═══════════════════════════════════════════
// Public API (диспетчеры)
// ═══════════════════════════════════════════

static ApiTestResult testOwmConnection(const String& apiKey, const String& lat, const String& lon) {
    ApiTestResult res;
    String url = "https://api.openweathermap.org/data/3.0/onecall?"
                 "lat=" + lat + "&lon=" + lon +
                 "&appid=" + apiKey +
                 "&units=" + String(OWM_UNITS) +
                 "&lang=" + String(OWM_LANG) +
                 "&exclude=minutely,hourly,daily,alerts";

    Serial.println("[TEST] Проверка OWM API-ключа...");
    HttpResult resp = httpGet(url);
    res.httpCode = resp.code;

    if (resp.code == HTTP_CODE_OK) {
        res.message = "OK";
    } else if (resp.code == 401) {
        res.message = "Неверный API-ключ";
    } else if (resp.code == 429) {
        res.message = "Превышен лимит запросов";
    } else if (resp.code <= 0) {
        res.message = "Нет соединения с сервером";
    } else {
        res.message = "HTTP " + String(resp.code);
        JsonDocument errDoc;
        if (!deserializeJson(errDoc, resp.body)) {
            String msg = errDoc["message"].as<String>();
            if (msg.length() > 0) res.message += ": " + msg;
        }
    }
    Serial.printf("[TEST] OWM: %d — %s\n", res.httpCode, res.message.c_str());
    return res;
}

static ApiTestResult testOmConnection(const String& lat, const String& lon) {
    ApiTestResult res;
    String url = "https://api.open-meteo.com/v1/forecast?"
                 "latitude=" + lat + "&longitude=" + lon +
                 "&current=temperature_2m"
                 "&forecast_days=1";

    Serial.println("[TEST] Проверка Open-Meteo...");
    HttpResult resp = httpGet(url);
    res.httpCode = resp.code;

    if (resp.code == HTTP_CODE_OK) {
        res.message = "OK";
    } else if (resp.code <= 0) {
        res.message = "Нет соединения с сервером";
    } else {
        res.message = "HTTP " + String(resp.code);
        JsonDocument errDoc;
        if (!deserializeJson(errDoc, resp.body)) {
            String reason = errDoc["reason"].as<String>();
            if (reason.length() > 0) res.message += ": " + reason;
        }
    }
    Serial.printf("[TEST] Open-Meteo: %d — %s\n", res.httpCode, res.message.c_str());
    return res;
}

ApiTestResult testApiConnection(const String& apiKey, const String& lat, const String& lon,
                                uint8_t provider) {
    if (provider == 1) return testOmConnection(lat, lon);
    return testOwmConnection(apiKey, lat, lon);
}

WeatherData fetchWeather() {
    if (g_settings.weatherProvider == 1) return fetchWeatherFromOm();
    return fetchWeatherFromOwm();
}
