// Botón físico con antirrebote: corto = capturar/enviar, largo = zero relativo.
#pragma once

#include <Arduino.h>

enum class ButtonEvent : uint8_t { NONE = 0, SHORT_PRESS, LONG_PRESS };

class Button {
public:
    void begin(uint8_t pin);
    ButtonEvent poll();  // llamar desde loop()

private:
    uint8_t  _pin = 0;
    bool     _pressed = false;
    bool     _longFired = false;
    uint32_t _lastChangeMs = 0;
    uint32_t _pressStartMs = 0;
    int      _lastRaw = HIGH;
};
