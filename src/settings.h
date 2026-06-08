// Configuración persistente en NVS (Preferences).
#pragma once

#include <Arduino.h>

enum class EolKey : uint8_t { NONE = 0, ENTER = 1, TAB = 2, SPACE = 3 };

#define WIFI_MAX_NETWORKS 10

struct WifiNet {
    String ssid;
    String pass;
    bool   staticIp = false;   // false = DHCP
    String ip;                 // válidos solo si staticIp
    String gateway;
    String subnet;
    String dns;
};

struct Settings {
    // redes WiFi guardadas: el equipo escanea y se conecta a la que
    // encuentre con mejor señal (casa, trabajo, hotspot del celu...)
    WifiNet wifi[WIFI_MAX_NETWORKS];
    uint8_t wifiCount;

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

    // devuelve la pass guardada para un SSID, o "" si no está
    String passFor(const String& ssid) const;
};

extern Settings settings;
