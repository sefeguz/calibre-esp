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
                      "lastRaw=0x%llX clk=%lumV data=%lumV heap=%lu "
                      "pins: CLK=GPIO%u DATA=GPIO%u%s\n",
                      (int)d.mode, d.on, (unsigned long)d.framesOk,
                      (unsigned long)d.framesBad, d.lastFrameBits,
                      (unsigned long long)d.lastFrameRaw, (unsigned long)d.clkMv,
                      (unsigned long)d.dataMv, (unsigned long)ESP.getFreeHeap(),
                      caliper.pinClk(), caliper.pinData(),
                      caliper.pinsSwapped() ? " (auto-intercambiados)" : "");
        Serial.printf("adc: maxBits=%u timeouts=%lu ventanas=%lu\n",
                      caliper.adcMaxBits, (unsigned long)caliper.adcBitTimeouts,
                      (unsigned long)caliper.adcWindows);
    }
    else if (cmd == "capture") {
        if (appCapture()) wsBroadcastCapture(appDisplayedMm());
    }
    else if (cmd == "zero") { appToggleRelative(); wsBroadcastStatus(); }
    else if (cmd == "blereset") {
        // borra los emparejamientos guardados (bonds viejos rotos hacen
        // fallar reconexiones); también hay que "olvidar" el dispositivo
        // del lado de la PC/celular y emparejar de nuevo
        bleKeyboard.clearBonds();
        Serial.println("[ble] bonds borrados; olvidar el dispositivo en el host y re-emparejar");
    }
    else if (cmd == "pins") {
        // Test de cableado: el pull-up interno (~45k) revela si la línea está
        // abierta (sube a ~VDD) o conectada a un nodo que la sujeta abajo.
        caliper.end();   // liberar ISR/tareas mientras manipulamos los pines
        pinMode(PIN_CALIPER_CLK, INPUT);
        pinMode(PIN_CALIPER_DATA, INPUT);
        delay(50);
        uint32_t clkF = analogReadMilliVolts(PIN_CALIPER_CLK);
        uint32_t dataF = analogReadMilliVolts(PIN_CALIPER_DATA);
        pinMode(PIN_CALIPER_CLK, INPUT_PULLUP);
        pinMode(PIN_CALIPER_DATA, INPUT_PULLUP);
        delay(50);
        uint32_t clkP = analogReadMilliVolts(PIN_CALIPER_CLK);
        uint32_t dataP = analogReadMilliVolts(PIN_CALIPER_DATA);
        pinMode(PIN_CALIPER_CLK, INPUT);     // restaurar entradas flotantes
        pinMode(PIN_CALIPER_DATA, INPUT);
        Serial.printf("flotante:  clk=%lumV data=%lumV\n",
                      (unsigned long)clkF, (unsigned long)dataF);
        Serial.printf("pull-up:   clk=%lumV data=%lumV\n",
                      (unsigned long)clkP, (unsigned long)dataP);
        Serial.println("pull-up >2000mV = linea ABIERTA (soldadura/cable)");
        Serial.println("pull-up <800mV  = conectada a un nodo que la sujeta abajo");
        CaliperMode m = settings.readMode == 1 ? CaliperMode::DIGITAL
                      : settings.readMode == 2 ? CaliperMode::ADC
                                               : CaliperMode::DETECTING;
        caliper.begin(m, settings.invert);
    }
    else if (cmd == "scope") {
        // Mini analizador lógico por ADC: muestrea CLK ~2 s y reporta los
        // intervalos entre transiciones para medir el clock real del calibre.
        caliper.end();
        const int MAXT = 200;
        static int64_t tTimes[MAXT];
        int n = 0;
        int samples = 0;
        bool lastHigh = analogRead(PIN_CALIPER_CLK) > 1000;  // ~0.8V en 12-bit/11db
        int64_t t0 = esp_timer_get_time();
        while (esp_timer_get_time() - t0 < 2000000 && n < MAXT) {
            bool high = analogRead(PIN_CALIPER_CLK) > 1000;
            samples++;
            if (high != lastHigh) {
                tTimes[n++] = esp_timer_get_time();
                lastHigh = high;
            }
        }
        int64_t span = esp_timer_get_time() - t0;
        Serial.printf("muestras=%d en %lld ms (~%.1f kS/s) | transiciones=%d\n",
                      samples, (long long)(span / 1000),
                      samples * 1000.0 / span, n);
        if (n > 1) {
            Serial.print("intervalos (us): ");
            for (int i = 1; i < n && i < 50; i++) {
                Serial.printf("%ld ", (long)(tTimes[i] - tTimes[i - 1]));
            }
            Serial.println();
        } else {
            Serial.println("sin transiciones: el calibre no esta transmitiendo "
                           "(o el clock es demasiado rapido para verlo)");
        }
        CaliperMode m = settings.readMode == 1 ? CaliperMode::DIGITAL
                      : settings.readMode == 2 ? CaliperMode::ADC
                                               : CaliperMode::DETECTING;
        caliper.begin(m, settings.invert);
    }
    else if (cmd.length()) {
        Serial.println("comandos: start | stop | status | redetect | capture | zero | pins | scope | blereset");
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

    // lecturas nuevas del calibre -> WebSocket + serial.
    // WS a tasa casi completa (cambios cada >=90 ms ~ 11 Hz) con flush
    // garantizado: si un cambio quedó suprimido por el intervalo, se envía
    // apenas vence — el valor final SIEMPRE llega en <100 ms. El guard de
    // cola llena en wsBroadcastReading evita el backlog con power-save.
    static bool wsDirty = false;
    CaliperReading r;
    if (caliper.poll(r)) {
        wsDirty = true;
        if (serialStream) {
            Serial.printf("{\"mm\":%.3f,\"counts\":%ld,\"unit\":\"%s\",\"ts\":%lu}\n",
                          appDisplayedMm(), (long)r.counts,
                          r.unit == CaliperUnit::INCH ? "in" : "mm",
                          (unsigned long)r.timestamp);
        }
    }
    {
        static float lastSentMm = -99999.0f;
        static uint32_t lastSentAt = 0;
        uint32_t nowMs = millis();
        float disp = appDisplayedMm();
        bool changed = fabsf(disp - lastSentMm) >= 0.005f;
        if (caliper.isOn() &&
            ((wsDirty && changed && nowMs - lastSentAt >= 90) ||
             nowMs - lastSentAt >= 500)) {
            wsBroadcastReading(caliper.lastAtomic(), disp, appRelativeActive());
            lastSentMm = disp;
            lastSentAt = nowMs;
            wsDirty = false;
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
