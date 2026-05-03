#include <WiFi.h>
#include <SPI.h>
#include <Preferences.h>
#include <driver/gpio.h>
#include <esp_bt.h>
#include "config.h"
#include "settings.h"
#include "weather_api.h"
#include "display_renderer.h"
#include "battery.h"
#include "sunny_tracker.h"
#include "admin_server.h"

// Дисплей
Display display(GxEPD2_397_GDEM0397T81(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// Глобальные runtime-настройки (определение, объявление в settings.h)
DeviceSettings g_settings;

// SSID точки доступа в режиме первичной настройки (без пароля).
// Поднимается в debug-режиме, если подключение к сохранённому WiFi не удалось.
constexpr const char* AP_SSID = "ESP-Weather-Setup";

// Сохраняем RSSI и IP до отключения WiFi
int lastRSSI = -100;
String deviceIP = "";

// Флаг успешной отрисовки — сохраняется через deep sleep.
// При ошибке WiFi/API оставляем предыдущие данные на e-ink вместо "Error".
RTC_DATA_ATTR bool hasDisplayedOnce = false;

// Timestamp последнего успешного fetchWeather — сохраняется в RTC RAM
// для отрисовки даты/времени в статус-баре при partial update после ошибки.
// 8 байт, переживает deep sleep но не power cycle.
RTC_DATA_ATTR long lastSuccessDt = 0;

// Флаг "сейчас работает админка" — определяет, что делает loop()
static bool adminMode = false;

bool connectWiFi() {
    Serial.print("Подключение к WiFi: ");
    Serial.println(g_settings.wifiSsid);

    WiFi.mode(WIFI_STA);
    WiFi.begin(g_settings.wifiSsid.c_str(), g_settings.wifiPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < WIFI_TIMEOUT_SEC * 2) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Публичный DNS вместо DNS роутера — надёжнее для внешних API
        // (роутер может кэшировать нерабочий IP из DNS round-robin)
        IPAddress dns1(8, 8, 8, 8);       // Google
        IPAddress dns2(1, 1, 1, 1);       // Cloudflare
        WiFi.config(WiFi.localIP(), WiFi.gatewayIP(), WiFi.subnetMask(), dns1, dns2);

        lastRSSI = WiFi.RSSI();
        deviceIP = WiFi.localIP().toString();
        Serial.printf("\nWiFi OK, IP: %s, RSSI: %d dBm, DNS: 8.8.8.8\n",
                      deviceIP.c_str(), lastRSSI);
        return true;
    }

    Serial.println("\nWiFi FAIL");
    return false;
}

void disconnectWiFi() {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    Serial.println("WiFi отключён");
}

// Поднимает открытую точку доступа для первичной настройки.
// Возвращает IP (обычно 192.168.4.1).
String startAccessPoint() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);  // open network — без авторизации (домашнее использование)
    delay(100);            // softAP инициализируется не мгновенно
    String ip = WiFi.softAPIP().toString();
    Serial.printf("AP запущена: SSID=%s, IP=%s\n", AP_SSID, ip.c_str());
    return ip;
}

// Режим отладки: USB подключён к компьютеру (SOF-фреймы) ИЛИ джампер замкнут.
// USB — основной триггер (подключил кабель = не спим, видим Serial).
// Джампер — аварийный вход (нет компьютера, но нужна админка через WiFi).
//
// HWCDC::isPlugged() на ESP32-S3 зависит от асинхронного SOF-колбэка USB-драйвера:
// после boot/wake колбэк успевает сработать только через ~100–300 мс. Поэтому USB
// опрашиваем циклом до обнаружения или таймаута USB_DETECT_TIMEOUT_MS. Джампер
// проверяем первым — он мгновенный и избавляет от ожидания, если отладка нужна
// без компьютера.
bool isDebugMode() {
    if (digitalRead(DEBUG_JUMPER_PIN) == LOW) return true;  // джампер на GND

    const unsigned long USB_DETECT_TIMEOUT_MS = 500;
    const unsigned long USB_POLL_INTERVAL_MS  = 25;
    unsigned long start = millis();
    while (millis() - start < USB_DETECT_TIMEOUT_MS) {
        if (HWCDC::isPlugged()) return true;
        delay(USB_POLL_INTERVAL_MS);
    }
    return false;
}

void goToSleep() {
    int sleepMin = g_settings.sleepDurationMin;
    // Защита от мусорных значений в NVS — clamp в разумный диапазон
    if (sleepMin < 1)    sleepMin = 1;
    if (sleepMin > 1440) sleepMin = 1440;  // макс сутки
    Serial.printf("Уходим в deep sleep на %d мин...\n", sleepMin);
    Serial.flush();   // ESP32-S3 USB CDC: гарантируем флаш буфера до sleep
    delay(50);        // страховка чтобы host успел вытянуть последние байты

    SPI.end();

    // Фиксируем состояние пинов дисплея — предотвращаем утечку тока
    gpio_hold_en((gpio_num_t)EPD_CS);
    gpio_hold_en((gpio_num_t)EPD_DC);
    gpio_hold_en((gpio_num_t)EPD_RST);
    gpio_hold_en((gpio_num_t)EPD_BUSY);
    gpio_deep_sleep_hold_en();

    esp_sleep_enable_timer_wakeup((uint64_t)sleepMin * 60ULL * 1000000ULL);
    esp_deep_sleep_start();
}

// Обработка ошибки обновления: первый запуск → показать error card,
// иначе → partial update только статус-бара (иконка ошибки + датавремя
// последнего успешного цикла), остальные зоны остаются на e-ink.
void handleUpdateFailure(const char* reason, int batteryPercent, int wifiRSSI) {
    if (!hasDisplayedOnce) {
        displayShowError(display, reason, batteryPercent, wifiRSSI, deviceIP.c_str(),
                         lastSuccessDt, g_settings.sleepDurationMin);
    } else {
        Serial.printf("%s — partial update статус-бара с иконкой ошибки\n", reason);
        displayUpdateStatusBarError(display, lastSuccessDt, batteryPercent, wifiRSSI,
                                    deviceIP.c_str(), g_settings.owmCity.c_str(),
                                    reason);
    }
}

void setup() {
    Serial.begin(115200);
    delay(20);
    Serial.println("\n=== Inkcast ===");

    // Bluetooth не используется — освобождаем память и выключаем контроллер.
    // Проверяем статус: после deep sleep контроллер может быть уже выключен.
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        esp_bt_controller_disable();
    }
    if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
        esp_bt_controller_deinit();
    }
    esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

    pinMode(DEBUG_JUMPER_PIN, INPUT_PULLUP);
    bool debugMode = isDebugMode();
    if (debugMode) {
        bool usb = HWCDC::isPlugged();
        bool jumper = digitalRead(DEBUG_JUMPER_PIN) == LOW;
        Serial.printf("Режим: ОТЛАДКА (%s%s%s)\n",
                       usb ? "USB" : "",
                       (usb && jumper) ? " + " : "",
                       jumper ? "джампер" : "");
    } else {
        Serial.println("Режим: продакшн");
    }

    // Загружаем пользовательские настройки из NVS (с fallback на defaults config.h)
    settingsLoad(g_settings);

    // Снимаем hold с пинов дисплея после deep sleep — иначе SPI/RST не работают
    gpio_hold_dis((gpio_num_t)EPD_CS);
    gpio_hold_dis((gpio_num_t)EPD_DC);
    gpio_hold_dis((gpio_num_t)EPD_RST);
    gpio_hold_dis((gpio_num_t)EPD_BUSY);

    // Инициализация дисплея: если уже был успешный рендер — без очистки экрана,
    // чтобы partial update статус-бара при ошибке не стёр остальные зоны.
    displayInit(display, /*initial=*/!hasDisplayedOnce);

    // Чтение батареи (до WiFi — точнее замер, один вызов ADC)
    BatteryReading battery = readBattery();
    int batteryPercent = voltageToPercent(battery.voltage);
    Serial.printf(
        "Батарея: %d%%  (%.2fV, ADC raw=%u, pin=%lumV)\n",
        batteryPercent,
        battery.voltage,
        battery.raw,
        battery.pinMilliVolts
    );

    // Подключение к WiFi
    if (!connectWiFi()) {
        // В debug-режиме поднимаем AP-fallback для первичной настройки.
        // В продакшне — обычное поведение: ошибка → deep sleep.
        if (debugMode) {
            Serial.println("WiFi недоступен — стартуем AP для настройки");
            String apIp = startAccessPoint();
            // Принудительно показываем AP-информацию (минуя hasDisplayedOnce),
            // иначе пользователь увидит старую погоду и не поймёт, что нужно настраивать.
            String banner = String("Setup mode | WiFi: ") + AP_SSID + " | http://" + apIp + "/";
            displayShowError(display, banner.c_str(), batteryPercent, 0, apIp.c_str());
            display.hibernate();
            adminServerBegin(AP_SSID, apIp);
            adminMode = true;
            return;  // дальше — loop() обслуживает админку
        }

        handleUpdateFailure("WiFi connection failed", batteryPercent, lastRSSI);
        disconnectWiFi();
        goToSleep();
        return;
    }

    // Получение погоды
    Serial.println("Запрос погоды...");
    WeatherData weather = fetchWeather();
    int rssi = lastRSSI;

    // В debug-режиме WiFi оставляем включённым для админки.
    // В продакшне — отключаем сразу после fetchWeather для экономии.
    if (!debugMode) {
        disconnectWiFi();
    }

    if (!weather.valid) {
        String reason = "Weather API: " + (weather.error.isEmpty() ? String("unknown") : weather.error);
        handleUpdateFailure(reason.c_str(), batteryPercent, rssi);
        if (debugMode) {
            // Даже при ошибке погоды — даём зайти в админку и поправить ключ/координаты
            adminServerBegin(g_settings.wifiSsid, deviceIP);
            adminMode = true;
            return;
        }
        goToSleep();
        return;
    }

    Serial.printf("Погода: %.1f°C, %s, icon=%s\n", weather.current.temp, weather.current.description.c_str(), weather.current.icon.c_str());

    // Обновляем трекер солнечных дней (NVS, 30-дневная история)
    bool isSunny = (weather.current.clouds < 50);
    int trackedDays = 0;
    weather.sunny_days = updateSunnyTracker(isSunny, weather.current.dt, trackedDays);
    weather.sunny_total_days = trackedDays;
    Serial.printf("Солнечных дней: %d/%d\n", weather.sunny_days, trackedDays);

    // Тренд давления (сравниваем с предыдущим значением из NVS)
    {
        Preferences prefs;
        prefs.begin("weather", false);
        int prevPressure = prefs.getInt("pressure", 0);
        int curPressure  = weather.current.pressure;
        if (prevPressure > 0) {
            int diff = curPressure - prevPressure;
            if (diff >= 2)       weather.pressure_trend =  1;  // растёт
            else if (diff <= -2) weather.pressure_trend = -1;  // падает
            else                 weather.pressure_trend =  0;  // стабильно
        }
        prefs.putInt("pressure", curPressure);
        prefs.end();
        Serial.printf("Давление: %d hPa (тренд: %+d)\n", curPressure, weather.pressure_trend);
    }

    // Отрисовка на дисплей
    displayRender(display, weather, batteryPercent, rssi, deviceIP.c_str());
    hasDisplayedOnce = true;
    lastSuccessDt = weather.current.dt;  // запоминаем для partial update при ошибке
    Serial.println("Дисплей обновлён");

    if (debugMode) {
        Serial.println("Deep sleep отключён (debug mode)");
        adminServerBegin(g_settings.wifiSsid, deviceIP);
        adminMode = true;
    } else {
        goToSleep();
    }
}

// Счётчик последовательных проверок, в которых debug-триггер отсутствует.
// Перезагрузка только после устойчивого отсутствия — защита от глитчей USB SOF.
static int debugGoneCount = 0;
static constexpr int DEBUG_GONE_THRESHOLD = 5000;  // ~5 сек при ~1 мс на итерацию loop

void loop() {
    // В debug-режиме обслуживаем админку. В продакшне до loop() не доходим — deep sleep.
    if (adminMode) {
        adminServerLoop();

        // Если USB выдернули и джампер разомкнут — перезагружаемся,
        // чтобы следующий setup() ушёл в продакшн (deep sleep).
        // Debounce: ждём устойчивого отсутствия, а не одиночный глитч.
        if (!isDebugMode()) {
            debugGoneCount++;
            if (debugGoneCount >= DEBUG_GONE_THRESHOLD) {
                Serial.println("Debug-триггер пропал — перезагрузка в продакшн");
                Serial.flush();
                delay(50);
                ESP.restart();
            }
        } else {
            debugGoneCount = 0;
        }
    }
}
