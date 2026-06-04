#include "button.h"
#include "config.h"

void Button::begin(uint8_t pin)
{
    _pin = pin;
    pinMode(_pin, INPUT_PULLUP);
    _lastRaw = digitalRead(_pin);
}

ButtonEvent Button::poll()
{
    int raw = digitalRead(_pin);
    uint32_t now = millis();

    if (raw != _lastRaw) {
        _lastRaw = raw;
        _lastChangeMs = now;
        return ButtonEvent::NONE;
    }

    if (now - _lastChangeMs < BUTTON_DEBOUNCE_MS) return ButtonEvent::NONE;

    bool down = (raw == LOW);

    if (down && !_pressed) {           // flanco presionado (estable)
        _pressed = true;
        _longFired = false;
        _pressStartMs = now;
    } else if (down && _pressed && !_longFired &&
               now - _pressStartMs >= BUTTON_LONGPRESS_MS) {
        _longFired = true;             // largo: dispara sin esperar soltar
        return ButtonEvent::LONG_PRESS;
    } else if (!down && _pressed) {    // soltado
        _pressed = false;
        if (!_longFired) return ButtonEvent::SHORT_PRESS;
    }

    return ButtonEvent::NONE;
}
