#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// Пользовательские настройки, редактируемые через web-админку.
// Хранятся в NVS namespace "config" и переживают deep sleep / перезагрузки.
// При отсутствии в NVS — берутся defaults из config.h (первая прошивка).
//
// Все ключи NVS укладываются в лимит 15 символов.

struct DeviceSettings {
    String wifiSsid;
    String wifiPassword;
    String owmApiKey;
    String owmLat;
    String owmLon;
    String owmCity;
    int    sleepDurationMin = SLEEP_DURATION_MIN;  // период обновления (минуты)
    uint8_t weatherProvider = 0;  // 0 = OpenWeatherMap, 1 = Open-Meteo
    String  omModel;               // модель Open-Meteo ("best_match", "ecmwf_ifs025", ...)
};

// Глобальный объект, заполняется один раз в setup() через settingsLoad()
extern DeviceSettings g_settings;

inline void settingsLoad(DeviceSettings& s) {
    Preferences prefs;
    prefs.begin("config", true);  // read-only
    s.wifiSsid         = prefs.getString("wifi_ssid", WIFI_SSID);
    s.wifiPassword     = prefs.getString("wifi_pass", WIFI_PASSWORD);
    s.owmApiKey        = prefs.getString("owm_key",   OWM_API_KEY);
    s.owmLat           = prefs.getString("owm_lat",   OWM_LAT);
    s.owmLon           = prefs.getString("owm_lon",   OWM_LON);
    s.owmCity          = prefs.getString("owm_city",  OWM_CITY);
    s.sleepDurationMin = prefs.getInt   ("sleep_min", SLEEP_DURATION_MIN);
    s.weatherProvider  = prefs.getUChar ("w_provider", 0);
    s.omModel          = prefs.getString("om_model",  "best_match");
    prefs.end();
}

// Возвращает true если все ключи записаны успешно
inline bool settingsSave(const DeviceSettings& s) {
    Preferences prefs;
    prefs.begin("config", false);  // read-write
    bool ok = true;
    ok &= prefs.putString("wifi_ssid", s.wifiSsid)   > 0;
    ok &= prefs.putString("wifi_pass", s.wifiPassword) > 0;
    ok &= prefs.putString("owm_key",   s.owmApiKey)   > 0;
    ok &= prefs.putString("owm_lat",   s.owmLat)      > 0;
    ok &= prefs.putString("owm_lon",   s.owmLon)      > 0;
    ok &= prefs.putString("owm_city",  s.owmCity)     > 0;
    ok &= prefs.putInt   ("sleep_min", s.sleepDurationMin) > 0;
    ok &= prefs.putUChar ("w_provider", s.weatherProvider) > 0;
    prefs.putString("om_model", s.omModel);  // пустая строка = best_match
    prefs.end();
    return ok;
}

// Очищает namespace "config" в NVS — после reboot настройки берутся из defaults config.h
inline void settingsReset() {
    Preferences prefs;
    prefs.begin("config", false);
    prefs.clear();
    prefs.end();
}
