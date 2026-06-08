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
            // IP estática empaquetada como "ip|gw|sn|dns" (vacío = DHCP)
            String x = prefs.getString((String("wx") + i).c_str(), "");
            if (x.length()) {
                int p1 = x.indexOf('|'), p2 = x.indexOf('|', p1 + 1),
                    p3 = x.indexOf('|', p2 + 1);
                if (p1 > 0 && p2 > p1 && p3 > p2) {
                    wifi[i].staticIp = true;
                    wifi[i].ip      = x.substring(0, p1);
                    wifi[i].gateway = x.substring(p1 + 1, p2);
                    wifi[i].subnet  = x.substring(p2 + 1, p3);
                    wifi[i].dns     = x.substring(p3 + 1);
                }
            } else {
                wifi[i].staticIp = false;
            }
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
        String ks = String("ws") + i, kp = String("wp") + i, kx = String("wx") + i;
        if (i < wifiCount) {
            prefs.putString(ks.c_str(), wifi[i].ssid);
            prefs.putString(kp.c_str(), wifi[i].pass);
            if (wifi[i].staticIp && wifi[i].ip.length()) {
                prefs.putString(kx.c_str(), wifi[i].ip + "|" + wifi[i].gateway +
                                "|" + wifi[i].subnet + "|" + wifi[i].dns);
            } else {
                prefs.remove(kx.c_str());
            }
        } else {
            prefs.remove(ks.c_str());
            prefs.remove(kp.c_str());
            prefs.remove(kx.c_str());
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
