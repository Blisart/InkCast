#pragma once

#include <Arduino.h>
#include "config.h"

struct BatteryReading {
    uint16_t raw;
    uint32_t pinMilliVolts;
    float voltage;
};

// Чтение напряжения батареи с калибровкой АЦП ESP32.
inline BatteryReading readBattery() {
    analogReadResolution(12);
    analogSetPinAttenuation(BAT_ADC_PIN, ADC_11db);
    pinMode(BAT_ADC_PIN, INPUT);

    uint32_t rawSum = 0;
    uint32_t mvSum = 0;
    for (int i = 0; i < 16; i++) {
        rawSum += analogRead(BAT_ADC_PIN);
        mvSum += analogReadMilliVolts(BAT_ADC_PIN);
    }

    BatteryReading reading;
    reading.raw = rawSum / 16;
    reading.pinMilliVolts = mvSum / 16;
    reading.voltage = (reading.pinMilliVolts / 1000.0f) * BAT_DIVIDER_RATIO;
    return reading;
}

inline float readBatteryVoltage() {
    return readBattery().voltage;
}

// Кусочно-линейная аппроксимация кривой разряда Li-Ion
inline int voltageToPercent(float v) {
    struct Point { float voltage; int percent; };
    static const Point curve[] = {
        {4.20f, 100}, {4.10f, 90}, {4.00f, 80},
        {3.90f,  60}, {3.80f, 40}, {3.70f, 20},
        {3.60f,  10}, {3.50f,  5}, {3.20f,  0}
    };
    static const int N = sizeof(curve) / sizeof(curve[0]);

    if (v >= curve[0].voltage) return 100;
    if (v <= curve[N - 1].voltage) return 0;

    for (int i = 0; i < N - 1; i++) {
        if (v >= curve[i + 1].voltage) {
            float ratio = (v - curve[i + 1].voltage) / (curve[i].voltage - curve[i + 1].voltage);
            return curve[i + 1].percent + (int)(ratio * (curve[i].percent - curve[i + 1].percent));
        }
    }
    return 0;
}
