// Teclado BLE HID (HID over GATT) minimalista sobre NimBLE-Arduino 2.x.
//
// Implementación propia: la lib clásica T-vK/ESP32-BLE-Keyboard está rota en
// ESP32-C3 (issue #275: conecta pero no tipea, GATT_INSUF_ENCRYPTION). Solo
// necesitamos dígitos, signo, separador decimal y Enter/Tab — y usamos
// keycodes que producen el mismo carácter en layouts US/ES/LatAm:
//   dígitos = fila superior, '-' = menos del keypad (0x56),
//   ',' = tecla coma (0x36), '.' = tecla punto (0x37).
#pragma once

#include <Arduino.h>
#include "settings.h"   // EolKey

class BleKeyboardOut {
public:
    void begin(const char* deviceName);

    // "Listo para tipear": enlace establecido Y host suscripto al input report
    // (recién ahí las notificaciones no se descartan — antes de la suscripción
    // NimBLE las tira silenciosamente).
    bool isConnected() const { return _connected && _subscribed; }

    // Tipea el texto (solo [0-9.,\-\t\n ]) en el host conectado.
    // Devuelve false si no hay host.
    bool typeText(const String& text);

    // Formatea y tipea una medición: valor + tecla final.
    // decimals según unidad (mm: 2, inch: 4). sep: ',' o '.'.
    bool typeMeasurement(float value, uint8_t decimals, char sep, EolKey eol);

    // Borra los emparejamientos guardados (bonds rotos impiden reconectar).
    void clearBonds();

    // uso interno (callbacks NimBLE)
    void setConnected(bool c) { _connected = c; if (!c) _subscribed = false; }
    void setSubscribed(bool s) { _subscribed = s; }

private:
    void sendKey(uint8_t keycode);
    void* _input = nullptr;    // NimBLECharacteristic* (opaco para no arrastrar headers)
    volatile bool _connected = false;
    volatile bool _subscribed = false;
    bool _started = false;
};

extern BleKeyboardOut bleKeyboard;
