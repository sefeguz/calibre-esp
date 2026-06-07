#include "settings.h"
#include "config.h"

#include <Preferences.h>

Settings settings;

static const char* NVS_NS = "calibre";

void Settings::load()
{
    Preferences prefs;
    prefs.begin(NVS_NS, true);

    // redes WiFi: formato nuevo "wifin"+"ws0..wp4"; migrar del formato
    // viejo de una sola red ("ssid"/"pass") si todavía no existe
    uint8_t n = prefs.getUChar("wifin", 255);
    if (n == 255) {
        String s = prefs.getString("ssid", "");
        String p = prefs.getString("pass", "");
        wifiCount = 0;
        if (s.length()) {
            wifi[0].ssid = s;
            wifi[0].pass = p;
            wifiCount = 1;
        }
    } else {
        wifiCount = n > WIFI_MAX_NETWORKS ? WIFI_MAX_NETWORKS : n;
        for (uint8_t i = 0; i < wifiCount; i++) {
            wifi[i].ssid = prefs.getString((String("ws") + i).c_str(), "");
            wifi[i].pass = prefs.getString((String("wp") + i).c_str(), "");
        }
    }

    deviceName = prefs.getString("name", DEVICE_NAME_DEFAULT);
    decimalSep = prefs.getChar("sep", ',');
    eolKey     = (EolKey)prefs.getUChar("eol", (uint8_t)EolKey::ENTER);
    bleEnabled = prefs.getBool("ble", true);
    readMode   = prefs.getUChar("rmode", 0);
    invert     = prefs.getBool("inv", false);
    prefs.end();
}

void Settings::save() const
{
    Preferences prefs;
    prefs.begin(NVS_NS, false);

    prefs.putUChar("wifin", wifiCount);
    for (uint8_t i = 0; i < WIFI_MAX_NETWORKS; i++) {
        String ks = String("ws") + i, kp = String("wp") + i;
        if (i < wifiCount) {
            prefs.putString(ks.c_str(), wifi[i].ssid);
            prefs.putString(kp.c_str(), wifi[i].pass);
        } else {
            prefs.remove(ks.c_str());
            prefs.remove(kp.c_str());
        }
    }
    prefs.remove("ssid");   // claves del formato viejo
    prefs.remove("pass");

    prefs.putString("name", deviceName);
    prefs.putChar("sep", decimalSep);
    prefs.putUChar("eol", (uint8_t)eolKey);
    prefs.putBool("ble", bleEnabled);
    prefs.putUChar("rmode", readMode);
    prefs.putBool("inv", invert);
    prefs.end();
}

void Settings::resetWifi()
{
    wifiCount = 0;
    save();
}

String Settings::passFor(const String& ssid) const
{
    for (uint8_t i = 0; i < wifiCount; i++) {
        if (wifi[i].ssid == ssid) return wifi[i].pass;
    }
    return "";
}
