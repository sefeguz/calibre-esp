// Calibre-ESP — lectura del puerto SPC de un calibre digital con ESP32-C3.
// Salidas: webserver WiFi (UI + WebSocket + REST), teclado BLE HID y serial.
// Botón físico: corto = capturar/tipear medición, largo = zero relativo.
//
// Basado en MGX3D/EspDRO (MIT) y jkent/esp32-caliper (MIT).
#include <Arduino.h>

#include "config.h"
#include "app.h"
#include "settings.h"
#include "button.h"
#include "ble_keyboard.h"
#include "webserver.h"

static Button button;
static bool serialStream = true;

// ---------------------------------------------------------------------------
// LED de estado:
//   AP (configuración WiFi): parpadeo rápido
//   normal: latido corto cada 2 s (más frecuente si el calibre está activo)
//   captura: ráfaga de 2 destellos (override momentáneo)
// ---------------------------------------------------------------------------
static uint32_t ledOverrideUntil = 0;

static void ledWrite(bool on)
{
    digitalWrite(PIN_LED, LED_INVERTED ? !on : on);
}

static void ledFlashCapture()
{
    ledOverrideUntil = millis() + 400;
}

static void ledLoop()
{
    uint32_t now = millis();
    if (now < ledOverrideUntil) {            // ráfaga de captura
        ledWrite((now / 60) % 2);
        return;
    }
    if (webserverInApMode()) {               // modo AP: rápido
        ledWrite((now / 150) % 2);
        return;
    }
    uint32_t period = caliper.isOn() ? 1000 : 2500;
    ledWrite((now % period) < 60);           // latido corto
}

// ---------------------------------------------------------------------------
// Serial: stream JSON de lecturas + comandos de diagnóstico
// ---------------------------------------------------------------------------
static void serialLoop()
{
    if (Serial.available() <= 0) return;
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd == "stop")  { serialStream = false; Serial.println("[ok] stream off"); }
    else if (cmd == "start") { serialStream = true; Serial.println("[ok] stream on"); }
    else if (cmd == "redetect") { caliper.redetect(); Serial.println("[ok] redetectando"); }
    else if (cmd == "status" || cmd == "diag") {
        CaliperDiag d = caliper.diag();
        Serial.printf("modo=%d on=%d framesOk=%lu framesBad=%lu lastBits=%u "
                      "lastRaw=0x%llX clk=%lumV data=%lumV heap=%lu\n",
                      (int)d.mode, d.on, (unsigned long)d.framesOk,
                      (unsigned long)d.framesBad, d.lastFrameBits,
                      (unsigned long long)d.lastFrameRaw, (unsigned long)d.clkMv,
                      (unsigned long)d.dataMv, (unsigned long)ESP.getFreeHeap());
    }
    else if (cmd == "capture") {
        if (appCapture()) wsBroadcastCapture(appDisplayedMm());
    }
    else if (cmd == "zero") { appToggleRelative(); wsBroadcastStatus(); }
    else if (cmd.length()) {
        Serial.println("comandos: start | stop | status | redetect | capture | zero");
    }
}

// ---------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.printf("\nCalibre-ESP v%s\n", FIRMWARE_VERSION);

    pinMode(PIN_LED, OUTPUT);
    ledWrite(false);

    settings.load();
    button.begin(PIN_BUTTON);

    // lector del calibre (auto-detección o modo forzado por configuración)
    CaliperMode mode = settings.readMode == 1 ? CaliperMode::DIGITAL
                     : settings.readMode == 2 ? CaliperMode::ADC
                                              : CaliperMode::DETECTING;
    caliper.begin(mode, settings.invert);

    // BLE primero (reserva memoria antes de que WiFi fragmente el heap)
    if (settings.bleEnabled) {
        bleKeyboard.begin(settings.deviceName.c_str());
        Serial.printf("[ble] teclado '%s' anunciando\n", settings.deviceName.c_str());
    }

    webserverBegin();

    Serial.printf("[mem] heap libre: %lu\n", (unsigned long)ESP.getFreeHeap());
    Serial.println("comandos serial: start | stop | status | redetect | capture | zero");
}

void loop()
{
    // pedidos llegados desde los handlers HTTP (tarea async): ejecutarlos acá,
    // en la tarea del loop, para que el tipeo BLE y las mutaciones de estado
    // nunca corran en la tarea de red
    if (appConsumeCaptureRequest()) {
        if (appCapture()) {
            wsBroadcastCapture(appDisplayedMm());
            ledFlashCapture();
        }
    }
    if (appConsumeZeroRequest()) {
        appToggleRelative();
        wsBroadcastStatus();
    }
    if (appConsumeClearRequest()) {
        appClearCaptures();
    }

    // lecturas nuevas del calibre -> WebSocket + serial
    CaliperReading r;
    if (caliper.poll(r)) {
        float disp = appDisplayedMm();
        wsBroadcastReading(r, disp, appRelativeActive());
        if (serialStream) {
            Serial.printf("{\"mm\":%.3f,\"counts\":%ld,\"unit\":\"%s\",\"ts\":%lu}\n",
                          disp, (long)r.counts,
                          r.unit == CaliperUnit::INCH ? "in" : "mm",
                          (unsigned long)r.timestamp);
        }
    }

    // botón físico
    switch (button.poll()) {
        case ButtonEvent::SHORT_PRESS:
            if (appCapture()) {
                wsBroadcastCapture(appDisplayedMm());
                ledFlashCapture();
            }
            break;
        case ButtonEvent::LONG_PRESS:
            appToggleRelative();
            wsBroadcastStatus();
            ledFlashCapture();
            break;
        default:
            break;
    }

    // estado periódico a los clientes web (BLE conectado, modo, on/off)
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 2000) {
        lastStatus = millis();
        wsBroadcastStatus();
    }

    webserverLoop();
    serialLoop();
    ledLoop();
    delay(2);  // ceder CPU (single-core)
}
