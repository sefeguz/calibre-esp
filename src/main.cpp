// Calibre-ESP — lectura del puerto SPC de un calibre digital con ESP32-C3.
// Salidas: webserver WiFi (UI + WebSocket + REST), teclado BLE HID y serial.
// Botón físico: corto = capturar/tipear medición, largo = zero relativo.
//
// Basado en MGX3D/EspDRO (MIT) y jkent/esp32-caliper (MIT).
#include <Arduino.h>

#include "config.h"
#include "app.h"
#include "settings.h"
#include "session.h"
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

// Acción de captura unificada (botón físico, web o serial — corre en loop):
// con sesión de medición activa, el valor va al ítem actual de la sesión;
// si no, al log de capturas (+ tipeo BLE).
static bool captureAction();

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
    if (webserverWifiEnabled()) {
        if (webserverInApMode()) {           // configurando (hotspot): parpadeo rápido
            ledWrite((now / 150) % 2);
        } else {                             // WiFi conectado: LED FIJO
            ledWrite(true);
        }
        return;
    }
    // WiFi apagado (solo BLE): latido corto = "encendido"
    uint32_t period = caliper.isOn() ? 1000 : 2500;
    ledWrite((now % period) < 60);
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
        Serial.print("wifi: ");
        Serial.println(webserverWifiInfo());
    }
    else if (cmd == "reboot") { Serial.println("[ok] reiniciando..."); delay(100); ESP.restart(); }
    else if (cmd == "wifi on")  { webserverSetWifi(true);  Serial.println("[ok] wifi on"); }
    else if (cmd == "wifi off") { webserverSetWifi(false); Serial.println("[ok] wifi off"); }
    else if (cmd == "wifi") { Serial.printf("[wifi] %s\n", webserverWifiEnabled() ? "on" : "off"); }
    else if (cmd == "capture") { captureAction(); }
    else if (cmd == "zero") { appToggleRelative(); wsBroadcastStatus(); }
    else if (cmd.startsWith("sim ")) {
        // inyecta una lectura simulada (pruebas sin calibre): sim 12.34
        float mm = cmd.substring(4).toFloat();
        caliper.injectReading(mm);
        CaliperReading r = caliper.lastAtomic();
        wsBroadcastReading(r, appDisplayedMm(), appRelativeActive());
        Serial.printf("[sim] lectura inyectada: %.2f mm\n", mm);
    }
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
        Serial.println("comandos: start | stop | status | redetect | capture | zero | sim <mm> | pins | scope | blereset");
    }
}

// ---------------------------------------------------------------------------

void setup()
{
    Serial.begin(115200);
    delay(100);
    Serial.printf("\nCalibre-ESP v%s\n", FIRMWARE_VERSION);

#ifdef BOARD_XIAO_C6
    // XIAO C6: seleccionar la antena integrada (GPIO14=LOW) y habilitar el
    // switch RF (GPIO3=LOW). Sin esto el WiFi/BLE puede tener poco alcance.
    pinMode(PIN_RF_SWITCH_EN, OUTPUT);
    digitalWrite(PIN_RF_SWITCH_EN, LOW);
    pinMode(PIN_ANT_SELECT, OUTPUT);
    digitalWrite(PIN_ANT_SELECT, LOW);
    Serial.println("[board] XIAO ESP32-C6, antena integrada");
#endif

    pinMode(PIN_LED, OUTPUT);
    ledWrite(false);

    settings.load();
#if PIN_BUTTON >= 0
    button.begin(PIN_BUTTON);
#endif

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

static bool captureAction()
{
    if (!caliper.isOn() || !caliper.hasReading()) return false;

    if (Session::isActive() && !Session::isConfirmed()) {
        // sesión de medición guiada: el valor llena el ítem actual
        if (Session::record(appDisplayedMm())) {
            wsBroadcastSession();
            ledFlashCapture();
            return true;
        }
        return false;
    }

    if (appCapture()) {                 // captura normal: log + tipeo BLE
        wsBroadcastCapture(appDisplayedMm());
        ledFlashCapture();
        return true;
    }
    return false;
}

void loop()
{
    // pedidos llegados desde los handlers HTTP (tarea async): ejecutarlos acá,
    // en la tarea del loop, para que el tipeo BLE y las mutaciones de estado
    // nunca corran en la tarea de red
    if (appConsumeCaptureRequest()) {
        captureAction();
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

    // botón físico (si la placa lo tiene; en la C6 sin botón la captura es
    // por web/MCP)
#if PIN_BUTTON >= 0
    switch (button.poll()) {
        case ButtonEvent::SHORT_PRESS:
            captureAction();
            break;
        case ButtonEvent::LONG_PRESS:
#ifdef WIFI_OFF_BY_DEFAULT
            // C6 (batería): mantener BOOT 2 s prende/apaga el WiFi
            webserverSetWifi(!webserverWifiEnabled());
            ledFlashCapture();
#else
            // C3 (banco): mantener 2 s = zero relativo
            appToggleRelative();
            wsBroadcastStatus();
            ledFlashCapture();
#endif
            break;
        default:
            break;
    }
#endif

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
