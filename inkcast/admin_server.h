#pragma once

#include <Arduino.h>

// Web-админка для настройки WiFi, OWM ключа и локации.
// Активна только в режиме отладки (джампер DEBUG_JUMPER_PIN на GND).
// При неудачном подключении к WiFi (в debug-режиме) поднимается AP-fallback,
// и админка работает на 192.168.4.1.
//
// HTTP, без авторизации (расчёт на домашнюю сеть).

// Запускает WebServer на порту 80.
// ssidForBanner — отображается на странице как имя сети (для подсказки)
// ipForBanner   — адрес, на котором админка сейчас доступна
void adminServerBegin(const String& ssidForBanner, const String& ipForBanner);

// Должно вызываться из loop() — обрабатывает входящие запросы и плановые перезагрузки.
void adminServerLoop();
