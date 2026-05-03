#include "admin_server.h"
#include "settings.h"
#include "weather_api.h"
#include <WebServer.h>
#include <WiFi.h>

// ═══════════════════════════════════════════
// State
// ═══════════════════════════════════════════

static WebServer server(80);
static String s_ssidBanner;
static String s_ipBanner;

// Запрос на перезагрузку — выполняется в adminServerLoop() с задержкой,
// чтобы успеть отдать HTTP-ответ браузеру до restart()
static bool s_rebootRequested = false;
static unsigned long s_rebootAt = 0;

// ═══════════════════════════════════════════
// Validation
// ═══════════════════════════════════════════

// Простая проверка: строка целиком — float в нужном диапазоне
static bool parseFloatInRange(const String& s, float lo, float hi) {
    if (s.isEmpty()) return false;
    char* end = nullptr;
    float v = strtof(s.c_str(), &end);
    if (end == s.c_str() || *end != 0) return false;
    return v >= lo && v <= hi;
}

// Парсит целое в диапазоне [lo, hi]; при успехе пишет результат в out.
// Пустая строка или мусор → false (пусть caller покажет ошибку).
static bool parseIntInRange(const String& s, int lo, int hi, int& out) {
    if (s.isEmpty()) return false;
    char* end = nullptr;
    long v = strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != 0) return false;
    if (v < lo || v > hi) return false;
    out = (int)v;
    return true;
}

// HTML-escape для подстановки value текущих настроек в форму
// (защита от поломки разметки на символах вроде " или &)
static String htmlEscape(const String& in) {
    String out;
    out.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;
        }
    }
    return out;
}

// ═══════════════════════════════════════════
// Render
// ═══════════════════════════════════════════

static String renderForm(const String& message = "", bool isError = false) {
    DeviceSettings cur;
    settingsLoad(cur);

    String html;
    html.reserve(5120);
    html += F("<!DOCTYPE html><html lang='ru'><head>"
              "<meta charset='UTF-8'>"
              "<meta name='viewport' content='width=device-width,initial-scale=1'>"
              "<title>Inkcast Setup</title>"
              "<style>"
              "body{font-family:system-ui,-apple-system,sans-serif;max-width:480px;margin:1em auto;padding:0 1em;color:#222;}"
              "h1{font-size:1.4em;margin-bottom:.3em;}"
              "h2{font-size:1.05em;margin-top:1.5em;color:#444;border-bottom:1px solid #ddd;padding-bottom:.2em;}"
              "label{display:block;margin:.7em 0 .2em;font-size:.85em;color:#555;}"
              "input[type=text]{width:100%;padding:.55em;font-size:1em;box-sizing:border-box;border:1px solid #bbb;border-radius:4px;}"
              "input[type=text]:focus{outline:none;border-color:#37c;}"
              "select{width:100%;padding:.55em;font-size:1em;box-sizing:border-box;border:1px solid #bbb;border-radius:4px;background:#fff;}"
              "button{margin-top:1.2em;padding:.7em 1.4em;font-size:1em;background:#2b7;color:#fff;border:0;border-radius:4px;cursor:pointer;}"
              "button:hover{background:#248;}"
              "button.reset{background:#a44;margin-top:.5em;}"
              "button.reset:hover{background:#822;}"
              ".banner{background:#eef;padding:.6em .8em;border-radius:4px;font-size:.85em;margin-bottom:1em;}"
              ".msg{padding:.6em .8em;border-radius:4px;margin-bottom:1em;}"
              ".msg.ok{background:#efe;color:#272;}"
              ".msg.err{background:#fee;color:#822;}"
              "form.reset-form{margin-top:2.5em;border-top:1px solid #eee;padding-top:1em;}"
              ".test-row{display:flex;gap:.5em;align-items:center;margin-top:.3em;}"
              ".test-row input{flex:1;}"
              "button.test-btn{margin:0;padding:.55em .8em;font-size:.85em;background:#47a;white-space:nowrap;}"
              "button.test-btn:hover{background:#358;}"
              "button.test-btn:disabled{background:#999;cursor:wait;}"
              "#test-result{font-size:.85em;margin-top:.3em;min-height:1.3em;}"
              "</style></head><body>");

    html += F("<h1>Настройка погодной станции</h1>");
    html += "<div class='banner'>Сеть: <b>" + htmlEscape(s_ssidBanner) +
            "</b><br>Адрес: <b>http://" + htmlEscape(s_ipBanner) + "/</b></div>";

    if (!message.isEmpty()) {
        html += String("<div class='msg ") + (isError ? "err" : "ok") + "'>" + message + "</div>";
    }

    html += F("<form method='POST' action='/save'>");

    html += F("<h2>WiFi</h2>");
    html += "<label>Имя сети (SSID)</label>"
            "<input type='text' name='wifi_ssid' value='" + htmlEscape(cur.wifiSsid) + "' required>";
    html += "<label>Пароль</label>"
            "<input type='text' name='wifi_pass' value='" + htmlEscape(cur.wifiPassword) + "'>";

    html += F("<h2>Провайдер погоды</h2>");
    html += "<label>Источник данных</label>"
            "<select name='w_provider' id='w_provider' onchange='toggleProvider()'>"
            "<option value='0'" + String(cur.weatherProvider == 0 ? " selected" : "") + ">OpenWeatherMap</option>"
            "<option value='1'" + String(cur.weatherProvider == 1 ? " selected" : "") + ">Open-Meteo (без ключа)</option>"
            "</select>";

    html += F("<div id='owm-section'>");
    html += F("<h2>OpenWeatherMap</h2>");
    html += "<label>API ключ</label>"
            "<div class='test-row'>"
            "<input type='text' name='owm_key' id='owm_key' value='" + htmlEscape(cur.owmApiKey) + "'>"
            "<button type='button' class='test-btn' id='test-btn-owm' onclick='testOwm()'>Проверить</button>"
            "</div>"
            "<div id='test-result-owm'></div>";
    html += F("</div>");

    html += F("<div id='om-section'>"
              "<h2>Open-Meteo</h2>"
              "<p style='font-size:.85em;color:#555;margin:.5em 0'>API ключ не требуется. "
              "Бесплатно, до 10 000 запросов/день.</p>");
    html += "<label>Модель прогноза</label>"
            "<select name='om_model'>"
            "<option value='best_match'"    + String(cur.omModel == "best_match"    || cur.omModel.isEmpty() ? " selected" : "") + ">Best Match (авто)</option>"
            "<option value='ecmwf_ifs025'"  + String(cur.omModel == "ecmwf_ifs025"  ? " selected" : "") + ">ECMWF IFS (точный, 3-7 дней)</option>"
            "<option value='icon_seamless'" + String(cur.omModel == "icon_seamless" ? " selected" : "") + ">ICON (DWD, хорош для Европы/РФ)</option>"
            "<option value='gfs_seamless'"  + String(cur.omModel == "gfs_seamless"  ? " selected" : "") + ">GFS (NOAA, до 16 дней)</option>"
            "<option value='gem_seamless'"  + String(cur.omModel == "gem_seamless"  ? " selected" : "") + ">GEM (Канада)</option>"
            "</select>";
    html += F("<div class='test-row' style='margin-top:.5em'>"
              "<span style='font-size:.85em'>Соединение:</span>"
              "<button type='button' class='test-btn' id='test-btn-om' onclick='testOm()'>Проверить</button>"
              "</div>"
              "<div id='test-result-om'></div>"
              "</div>");

    html += F("<h2>Локация</h2>");
    html += "<label>Широта (latitude, -90..90)</label>"
            "<input type='text' name='owm_lat' value='" + htmlEscape(cur.owmLat) + "' required>";
    html += "<label>Долгота (longitude, -180..180)</label>"
            "<input type='text' name='owm_lon' value='" + htmlEscape(cur.owmLon) + "' required>";
    html += "<label>Город (отображается на дисплее)</label>"
            "<input type='text' name='owm_city' value='" + htmlEscape(cur.owmCity) + "' required>";

    html += F("<h2>Расписание</h2>");
    html += "<label>Период обновления (минуты, 1..1440)</label>"
            "<input type='text' name='sleep_min' value='" + String(cur.sleepDurationMin) + "' required>";

    html += F("<button type='submit'>Сохранить и перезагрузить</button>"
              "</form>");

    html += F("<form method='POST' action='/reset' class='reset-form' "
              "onsubmit=\"return confirm('Сбросить все настройки к значениям из прошивки?');\">"
              "<button type='submit' class='reset'>Сбросить к заводским</button>"
              "</form>");

    html += F("<script>"
              "function toggleProvider(){"
              "var p=document.getElementById('w_provider').value;"
              "document.getElementById('owm-section').style.display=p==='0'?'':'none';"
              "document.getElementById('om-section').style.display=p==='1'?'':'none';}"
              "toggleProvider();"
              "function doTest(btn,res,body){"
              "btn.disabled=true;btn.textContent='...';"
              "res.style.color='#555';res.textContent='Запрос...';"
              "fetch('/test-api',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:body})"
              ".then(function(x){return x.json()})"
              ".then(function(d){res.style.color=d.ok?'#272':'#822';"
              "res.textContent=d.ok?'\\u2714 '+d.msg:'\\u2718 '+d.msg;})"
              ".catch(function(){res.style.color='#822';res.textContent='Ошибка сети';})"
              ".finally(function(){btn.disabled=false;btn.textContent='Проверить';});}"
              "function testOwm(){"
              "var k=document.getElementById('owm_key').value,"
              "la=document.querySelector('[name=owm_lat]').value,"
              "lo=document.querySelector('[name=owm_lon]').value;"
              "if(!k){var r=document.getElementById('test-result-owm');r.style.color='#822';r.textContent='Введите API ключ';return;}"
              "doTest(document.getElementById('test-btn-owm'),document.getElementById('test-result-owm'),"
              "'provider=0&key='+encodeURIComponent(k)+'&lat='+encodeURIComponent(la)+'&lon='+encodeURIComponent(lo));}"
              "function testOm(){"
              "var la=document.querySelector('[name=owm_lat]').value,"
              "lo=document.querySelector('[name=owm_lon]').value;"
              "doTest(document.getElementById('test-btn-om'),document.getElementById('test-result-om'),"
              "'provider=1&lat='+encodeURIComponent(la)+'&lon='+encodeURIComponent(lo));}"
              "</script>");

    html += F("</body></html>");
    return html;
}

static String renderRebootPage(const char* title, const char* line) {
    String html;
    html.reserve(512);
    html += F("<!DOCTYPE html><html lang='ru'><head>"
              "<meta charset='UTF-8'>"
              "<meta http-equiv='refresh' content='5; url=/'>"
              "<title>");
    html += title;
    html += F("</title>"
              "<style>body{font-family:system-ui,sans-serif;max-width:480px;margin:3em auto;text-align:center;color:#222;}"
              "h1{color:#272;}</style></head><body>"
              "<h1>");
    html += title;
    html += F("</h1><p>");
    html += line;
    html += F("</p></body></html>");
    return html;
}

// ═══════════════════════════════════════════
// Handlers
// ═══════════════════════════════════════════

static void handleRoot() {
    server.send(200, "text/html; charset=UTF-8", renderForm());
}

static void handleSave() {
    DeviceSettings s;
    s.wifiSsid     = server.arg("wifi_ssid");
    s.wifiPassword = server.arg("wifi_pass");
    s.owmApiKey    = server.arg("owm_key");
    s.owmLat       = server.arg("owm_lat");
    s.owmLon       = server.arg("owm_lon");
    s.owmCity      = server.arg("owm_city");
    String sleepStr    = server.arg("sleep_min");
    String providerStr = server.arg("w_provider");
    s.weatherProvider  = (providerStr == "1") ? 1 : 0;
    s.omModel          = server.arg("om_model");

    String error;
    if (s.wifiSsid.isEmpty()) {
        error = "SSID не может быть пустым";
    } else if (s.weatherProvider == 0 && s.owmApiKey.isEmpty()) {
        error = "API ключ OWM не может быть пустым";
    } else if (!parseFloatInRange(s.owmLat, -90.0f, 90.0f)) {
        error = "Широта должна быть числом от -90 до 90";
    } else if (!parseFloatInRange(s.owmLon, -180.0f, 180.0f)) {
        error = "Долгота должна быть числом от -180 до 180";
    } else if (s.owmCity.isEmpty()) {
        error = "Название города не может быть пустым";
    } else if (!parseIntInRange(sleepStr, 1, 1440, s.sleepDurationMin)) {
        error = "Период обновления должен быть целым числом от 1 до 1440 минут";
    }

    if (!error.isEmpty()) {
        server.send(400, "text/html; charset=UTF-8", renderForm("Ошибка: " + error, true));
        return;
    }

    if (!settingsSave(s)) {
        server.send(500, "text/html; charset=UTF-8",
                    renderForm("Ошибка записи в NVS — настройки не сохранены", true));
        return;
    }
    Serial.println("[ADMIN] Настройки сохранены, перезагрузка через 1с...");

    server.send(200, "text/html; charset=UTF-8",
                renderRebootPage("Сохранено", "Устройство перезагружается..."));

    s_rebootRequested = true;
    s_rebootAt = millis() + 1000;  // даём время отдать ответ браузеру
}

static void handleReset() {
    settingsReset();
    Serial.println("[ADMIN] Настройки сброшены, перезагрузка через 1с...");

    server.send(200, "text/html; charset=UTF-8",
                renderRebootPage("Сброшено", "Загружены значения по умолчанию. Перезагрузка..."));

    s_rebootRequested = true;
    s_rebootAt = millis() + 1000;
}

static void handleTestApi() {
    String key = server.arg("key");
    String lat = server.arg("lat");
    String lon = server.arg("lon");
    uint8_t provider = server.arg("provider").toInt();

    if (provider == 0 && key.isEmpty()) {
        server.send(400, "application/json", "{\"ok\":false,\"msg\":\"API ключ пустой\"}");
        return;
    }
    if (lat.isEmpty()) lat = g_settings.owmLat;
    if (lon.isEmpty()) lon = g_settings.owmLon;

    ApiTestResult res = testApiConnection(key, lat, lon, provider);
    bool ok = (res.httpCode == 200);

    String json = "{\"ok\":" + String(ok ? "true" : "false") +
                  ",\"code\":" + String(res.httpCode) +
                  ",\"msg\":\"" + htmlEscape(res.message) + "\"}";
    server.send(200, "application/json; charset=UTF-8", json);
}

static void handleNotFound() {
    server.send(404, "text/plain; charset=UTF-8", "404 Not Found");
}

// ═══════════════════════════════════════════
// API
// ═══════════════════════════════════════════

void adminServerBegin(const String& ssidForBanner, const String& ipForBanner) {
    s_ssidBanner = ssidForBanner;
    s_ipBanner   = ipForBanner;

    server.on("/",         HTTP_GET,  handleRoot);
    server.on("/save",     HTTP_POST, handleSave);
    server.on("/reset",    HTTP_POST, handleReset);
    server.on("/test-api", HTTP_POST, handleTestApi);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.printf("[ADMIN] Web-админка: http://%s/\n", ipForBanner.c_str());
}

void adminServerLoop() {
    server.handleClient();

    if (s_rebootRequested && (long)(millis() - s_rebootAt) >= 0) {
        delay(50);
        ESP.restart();
    }
}
