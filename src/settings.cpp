#include "settings.h"
#include "config.h"

#include <Preferences.h>

Settings settings;

static const char* NVS_NS = "calibre";

void Settings::load()
{
    Preferences prefs;
    prefs.begin(NVS_NS, true);
    wifiSsid   = prefs.getString("ssid", "");
    wifiPass   = prefs.getString("pass", "");
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
    prefs.putString("ssid", wifiSsid);
    prefs.putString("pass", wifiPass);
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
    wifiSsid = "";
    wifiPass = "";
    save();
}
