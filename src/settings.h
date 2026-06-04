// Configuración persistente en NVS (Preferences).
#pragma once

#include <Arduino.h>

enum class EolKey : uint8_t { NONE = 0, ENTER = 1, TAB = 2 };

struct Settings {
    // red
    String wifiSsid;
    String wifiPass;
    String deviceName;   // mDNS + nombre BLE

    // teclado BLE
    char   decimalSep;   // ',' (Excel es-AR) o '.'
    EolKey eolKey;       // tecla tras la medición
    bool   bleEnabled;

    // lectura
    uint8_t readMode;    // 0=auto, 1=digital, 2=ADC
    bool    invert;      // señales invertidas (level-shifter NPN)

    void load();
    void save() const;
    void resetWifi();
};

extern Settings settings;
