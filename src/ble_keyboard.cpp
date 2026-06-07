#include "ble_keyboard.h"
#include "config.h"

#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>

BleKeyboardOut bleKeyboard;

// Report map: teclado estándar boot-compatible, report ID 1, 6KRO.
static const uint8_t REPORT_MAP[] = {
    0x05, 0x01,  // Usage Page (Generic Desktop)
    0x09, 0x06,  // Usage (Keyboard)
    0xA1, 0x01,  // Collection (Application)
    0x85, 0x01,  //   Report ID (1)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0xE0,  //   Usage Min (224) — modificadores
    0x29, 0xE7,  //   Usage Max (231)
    0x15, 0x00,  //   Logical Min (0)
    0x25, 0x01,  //   Logical Max (1)
    0x75, 0x01,  //   Report Size (1)
    0x95, 0x08,  //   Report Count (8)
    0x81, 0x02,  //   Input (Data, Var, Abs)
    0x95, 0x01,  //   Report Count (1) — byte reservado
    0x75, 0x08,  //   Report Size (8)
    0x81, 0x01,  //   Input (Const)
    0x95, 0x06,  //   Report Count (6) — hasta 6 teclas
    0x75, 0x08,  //   Report Size (8)
    0x15, 0x00,  //   Logical Min (0)
    0x25, 0x65,  //   Logical Max (101)
    0x05, 0x07,  //   Usage Page (Key Codes)
    0x19, 0x00,  //   Usage Min (0)
    0x29, 0x65,  //   Usage Max (101)
    0x81, 0x00,  //   Input (Data, Array)
    0xC0         // End Collection
};

namespace {

class KbdServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* server, NimBLEConnInfo& connInfo) override {
        // OJO: no pedir updateConnParams acá — un update de parámetros en
        // pleno pairing hace fallar la conexión con Windows. El host HID
        // negocia sus propios parámetros.
        bleKeyboard.setConnected(true);
    }
    void onDisconnect(NimBLEServer* server, NimBLEConnInfo& connInfo, int reason) override {
        bleKeyboard.setConnected(false);
        NimBLEDevice::startAdvertising();  // volver a anunciar
    }
};

// El host queda "listo" recién cuando suscribe el CCCD del input report;
// notificar antes se descarta en silencio dentro de NimBLE.
class InputReportCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* c, NimBLEConnInfo& connInfo,
                     uint16_t subValue) override {
        bleKeyboard.setSubscribed((subValue & 0x0001) != 0);
    }
};

NimBLEHIDDevice* s_hid = nullptr;

// HID usage IDs idénticos en layouts US / ES-España / ES-LatAm
uint8_t charToKeycode(char c)
{
    if (c >= '1' && c <= '9') return 0x1E + (c - '1');  // fila superior 1..9
    switch (c) {
        case '0':  return 0x27;
        case '-':  return 0x56;  // keypad minus: '-' en todos los layouts
        case ',':  return 0x36;  // tecla coma (US/ES/LatAm)
        case '.':  return 0x37;  // tecla punto (US/ES/LatAm)
        case '\n': return 0x28;  // Enter
        case '\t': return 0x2B;  // Tab
        case ' ':  return 0x2C;  // espacio
        default:   return 0;
    }
}

} // namespace

void BleKeyboardOut::begin(const char* deviceName)
{
    if (_started) return;

    NimBLEDevice::init(deviceName);
    NimBLEDevice::setPower(6);   // dBm (la sobrecarga int8_t; el enum del IDF
                                 // tiene otros valores numéricos en el C3)
    // HID requiere encriptación: bonding + Just Works LEGACY (sc=false).
    // Con Secure Connections algunas pilas (Windows/Android viejos) fallan
    // el pairing con dispositivos NoInputNoOutput.
    NimBLEDevice::setSecurityAuth(true, false, false);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(new KbdServerCallbacks());

    s_hid = new NimBLEHIDDevice(server);
    s_hid->setManufacturer("Calibre-ESP");
    s_hid->setPnp(0x02, 0xE502, 0xA111, 0x0100);
    s_hid->setHidInfo(0x00, 0x01);
    s_hid->setReportMap((uint8_t*)REPORT_MAP, sizeof(REPORT_MAP));
    NimBLECharacteristic* input = s_hid->getInputReport(1);
    input->setCallbacks(new InputReportCallbacks());
    _input = input;
    s_hid->setBatteryLevel(100);
    // (los servicios GATT los arranca adv->start() vía NimBLEServer::start())

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAppearance(0x03C1);  // HID keyboard
    adv->addServiceUUID(s_hid->getHidService()->getUUID());
    adv->setName(deviceName);
    adv->start();

    _started = true;
}

void BleKeyboardOut::clearBonds()
{
    if (_started) NimBLEDevice::deleteAllBonds();
}

void BleKeyboardOut::sendKey(uint8_t keycode)
{
    if (!_input) return;
    auto* input = static_cast<NimBLECharacteristic*>(_input);

    uint8_t report[8] = {0};   // [mod, reservado, k1..k6]
    report[2] = keycode;
    input->setValue(report, sizeof(report));
    input->notify();
    delay(8);                  // separación entre eventos HID

    memset(report, 0, sizeof(report));
    input->setValue(report, sizeof(report));
    input->notify();
    delay(8);
}

bool BleKeyboardOut::typeText(const String& text)
{
    if (!_connected) return false;
    for (size_t i = 0; i < text.length(); i++) {
        uint8_t kc = charToKeycode(text[i]);
        if (kc) sendKey(kc);
    }
    return true;
}

bool BleKeyboardOut::typeMeasurement(float value, uint8_t decimals, char sep, EolKey eol)
{
    char buf[24];
    dtostrf(value, 0, decimals, buf);   // "12.34" (siempre con '.')

    String text(buf);
    text.trim();
    if (sep == ',') text.replace('.', ',');
    if (eol == EolKey::ENTER) text += '\n';
    else if (eol == EolKey::TAB) text += '\t';
    else if (eol == EolKey::SPACE) text += ' ';

    return typeText(text);
}
