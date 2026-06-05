#include "caliper.h"
#include "config.h"

#include <esp_timer.h>
#include <driver/gpio.h>

// El driver oneshot del ADC no tolera lecturas concurrentes (detectTask/adcTask
// vs diag() desde otra tarea): falla con "adc oneshot read fail" y devuelve 0.
// Serializar todas las lecturas con un mutex.
static SemaphoreHandle_t s_adcMutex = nullptr;

static uint32_t adcReadMv(uint8_t pin)
{
    if (s_adcMutex) xSemaphoreTake(s_adcMutex, portMAX_DELAY);
    uint32_t mv = analogReadMilliVolts(pin);
    if (s_adcMutex) xSemaphoreGive(s_adcMutex);
    return mv;
}

// Lectura cruda (sin calibración): ~50 µs vs ~70-100 µs de la calibrada.
// Imprescindible en el camino caliente: las fases del clock del calibre
// bajan a ~100 µs y con la lectura lenta se pierden fases enteras.
static int adcReadRaw(uint8_t pin)
{
    if (s_adcMutex) xSemaphoreTake(s_adcMutex, portMAX_DELAY);
    int v = analogRead(pin);
    if (s_adcMutex) xSemaphoreGive(s_adcMutex);
    return v;
}

// ---------------------------------------------------------------------------
// ISR modo digital — ANYEDGE en CLK, igual que jkent/esp32-caliper: el bit de
// DATA se acumula SOLO cuando CLK queda ALTO tras el flanco (= flanco
// ascendente, donde el dato es estable; verificado en ambas referencias que
// funcionan en hardware). Con `invert` (NPN) se niegan CLK y DATA.
//
// Framing por gap: si pasó > CALIPER_BIT_GAP_US desde el último flanco, el
// frame anterior terminó: se encola con su conteo de bits (24 = BIN6;
// 21/48 = variantes, se reportan en diagnóstico). LSB primero.
//
// Registrado vía gpio_isr_handler_add con el servicio instalado con
// ESP_INTR_FLAG_IRAM: sigue funcionando durante escrituras de flash (NVS/OTA).
// ---------------------------------------------------------------------------
void IRAM_ATTR Caliper::clkIsr(void* arg)
{
    Caliper* self = static_cast<Caliper*>(arg);
    int64_t now = esp_timer_get_time();

    int clk = gpio_get_level((gpio_num_t)self->_pinClk);
    int data = gpio_get_level((gpio_num_t)self->_pinData);
    if (self->_invert) { clk = !clk; data = !data; }

    if (self->_isrBits > 0 && now - self->_isrLastEdgeUs > CALIPER_BIT_GAP_US) {
        Frame f = { self->_isrShift, self->_isrBits };
        self->_isrShift = 0;
        self->_isrBits = 0;
        BaseType_t wake = pdFALSE;
        xQueueSendFromISR(self->_queue, &f, &wake);
        if (wake) portYIELD_FROM_ISR();
    }
    self->_isrLastEdgeUs = now;

    if (clk) {  // flanco ascendente efectivo: dato válido
        if (self->_isrBits < 60) {  // tope: nunca desbordar el shift de 64 bits
            self->_isrShift |= ((uint64_t)(data & 1)) << self->_isrBits;
            self->_isrBits = self->_isrBits + 1;  // (volatile++ deprecado en C++20)
        }
    }
}

// ---------------------------------------------------------------------------
// Modo ADC — polling con umbral por software (EspDRO) para señales de ~1.5 V.
// Secuencia probada de EspDRO: esperar CLK alto->bajo, luego bajo->alto, y
// muestrear DATA (mismo flanco ascendente que el modo digital). Solo viable
// con calibres de clock lento (<~4 kHz).
//
// C3 es single-core: un busy-loop permanente mata al idle task y dispara el
// watchdog. Estrategia: dormir ~60% del intervalo entre paquetes (medido
// adaptativamente) y quedar "caliente" solo alrededor de la ráfaga.
// ---------------------------------------------------------------------------
int Caliper::adcReadBit(uint32_t bitDeadlineMs)
{
    while (adcReadRaw(_pinClk) > CALIPER_ADC_THRESH_RAW) {
        if (millis() > bitDeadlineMs) return -1;
    }
    while (adcReadRaw(_pinClk) < CALIPER_ADC_THRESH_RAW) {
        if (millis() > bitDeadlineMs) return -1;
    }
    return adcReadRaw(_pinData) > CALIPER_ADC_THRESH_RAW ? 1 : 0;
}

void Caliper::adcTask(void* arg)
{
    Caliper* self = static_cast<Caliper*>(arg);
    uint32_t interFrameMs = 0;  // intervalo entre paquetes, medido en caliente

    for (;;) {
        // Ceder CPU entre paquetes: dormir ~55% del intervalo y quedar
        // "caliente" el resto, para cazar TODOS los paquetes (~9 Hz).
        if (interFrameMs >= 40) {
            vTaskDelay(pdMS_TO_TICKS(interFrameMs * 55 / 100));
        } else {
            vTaskDelay(1);
        }

        uint32_t packet = 0;
        int bitIndex = 0;
        bool ok = false;
        uint32_t packetDeadline = millis() + 300;

        while (millis() < packetDeadline) {
            int bit = self->adcReadBit(millis() + 30);
            if (bit < 0) {            // gap/timeout: frame trunco, reintentar
                self->adcBitTimeouts = self->adcBitTimeouts + 1;
                bitIndex = 0;
                packet = 0;
                continue;
            }
            packet |= ((uint32_t)bit) << bitIndex;
            bitIndex++;
            if (bitIndex > self->adcMaxBits) self->adcMaxBits = bitIndex;
            if (bitIndex == CALIPER_PACKET_BITS) { ok = true; break; }
        }
        self->adcWindows = self->adcWindows + 1;

        if (ok) {
            Frame f = { packet, CALIPER_PACKET_BITS };
            xQueueSend(self->_queue, &f, 0);

            uint32_t nowMs = millis();
            if (self->_lastAdcFrameMs) {
                uint32_t d = nowMs - self->_lastAdcFrameMs;
                if (d > 20 && d < 2000) {
                    // Seguir el MÍNIMO intervalo visto (el ritmo real del
                    // calibre); un promedio simple converge a "paquete por
                    // medio" si se pierde uno. Deriva lenta hacia arriba por
                    // si el calibre cambia de ritmo.
                    if (interFrameMs == 0 || d < interFrameMs) {
                        interFrameMs = d;
                    } else {
                        interFrameMs += (d - interFrameMs) / 16;
                    }
                }
            }
            self->_lastAdcFrameMs = nowMs;
        } else {
            interFrameMs = 0;                  // sin señal: olvidar el ritmo
            vTaskDelay(pdMS_TO_TICKS(50));     // y dormir bastante
        }
    }
}

// ---------------------------------------------------------------------------
// Auto-detección de modo Y de pines. Algunos calibres traen DATA/CLK en orden
// inverso en el conector: se identifica el CLK real porque SIEMPRE conmuta más
// que DATA (48 flancos por paquete contra ≤25).
//  1) flancos digitales en cada línea (señal 3 V) -> modo DIGITAL
//  2) cruces de umbral por ADC en ambas líneas (señal 1.5 V) -> modo ADC
// Reintenta hasta encontrar señal (el calibre puede estar apagado).
// ---------------------------------------------------------------------------
static volatile uint32_t s_detectEdges = 0;

static void IRAM_ATTR detectEdgeIsr(void*) { s_detectEdges = s_detectEdges + 1; }

// cuenta flancos digitales en un pin durante windowMs
static uint32_t countDigitalEdges(uint8_t pin, uint32_t windowMs)
{
    s_detectEdges = 0;
    gpio_set_intr_type((gpio_num_t)pin, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add((gpio_num_t)pin, detectEdgeIsr, nullptr);
    vTaskDelay(pdMS_TO_TICKS(windowMs));
    gpio_isr_handler_remove((gpio_num_t)pin);
    return s_detectEdges;
}

// cuenta cruces del umbral ADC en ambos pines (lecturas intercaladas)
static void countAdcCrossings(uint8_t pinA, uint8_t pinB, uint32_t windowMs,
                              uint32_t& crossA, uint32_t& crossB)
{
    crossA = crossB = 0;
    bool lastA = adcReadRaw(pinA) > CALIPER_ADC_THRESH_RAW;
    bool lastB = adcReadRaw(pinB) > CALIPER_ADC_THRESH_RAW;
    uint32_t deadline = millis() + windowMs;
    uint32_t lastYield = millis();
    while (millis() < deadline) {
        bool a = adcReadRaw(pinA) > CALIPER_ADC_THRESH_RAW;
        bool b = adcReadRaw(pinB) > CALIPER_ADC_THRESH_RAW;
        if (a != lastA) { crossA++; lastA = a; }
        if (b != lastB) { crossB++; lastB = b; }
        if (millis() - lastYield > 40) { lastYield = millis(); vTaskDelay(1); }
    }
}

void Caliper::detectTask(void* arg)
{
    Caliper* self = static_cast<Caliper*>(arg);

    for (;;) {
        // 1) ¿flancos digitales en CLK? (señal llega al VIH ~2.47 V)
        uint32_t edges = countDigitalEdges(PIN_CALIPER_CLK, CALIPER_DETECT_MS);
        Serial.printf("[detect] flancos digitales CLK(GPIO%d)=%lu\n",
                      PIN_CALIPER_CLK, (unsigned long)edges);

        if (edges >= 2 * CALIPER_PACKET_BITS - 8) {   // ~1 frame (2 flancos/bit)
            self->_detectTaskHandle = nullptr;
            self->startDigital();
            vTaskDelete(nullptr);
            return;
        }

        // 2) ¿actividad analógica en CLK? (señal de ~1.5 V)
        uint32_t c1, c2;
        countAdcCrossings(PIN_CALIPER_CLK, PIN_CALIPER_DATA,
                          2 * CALIPER_DETECT_MS, c1, c2);
        Serial.printf("[detect] cruces ADC: CLK(GPIO%d)=%lu DATA(GPIO%d)=%lu\n",
                      PIN_CALIPER_CLK, (unsigned long)c1,
                      PIN_CALIPER_DATA, (unsigned long)c2);

        if (c1 >= 24) {   // tráfico en CLK (al menos un paquete visto)
            self->_detectTaskHandle = nullptr;
            self->startAdc();
            vTaskDelete(nullptr);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));  // nada: reintentar
    }
}

// ---------------------------------------------------------------------------

void Caliper::begin(CaliperMode forceMode, bool invert)
{
    // Entradas flotantes: no inyectar corriente al calibre (corre de su pila)
    pinMode(PIN_CALIPER_DATA, INPUT);
    pinMode(PIN_CALIPER_CLK, INPUT);
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);  // rango ~0-2.5 V calibrado, cubre 1.5 V
    if (!s_adcMutex) s_adcMutex = xSemaphoreCreateMutex();

    // Servicio de ISR GPIO en IRAM: sobrevive a escrituras de flash (NVS/OTA).
    // Instalar una sola vez (re-begin tras redetect/pins no debe reinstalar).
    static bool isrServiceInstalled = false;
    if (!isrServiceInstalled) {
        esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
        if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
            isrServiceInstalled = true;
        } else {
            log_e("gpio_install_isr_service: %d", err);
        }
    }

    if (!_queue) _queue = xQueueCreate(8, sizeof(Frame));
    _forceMode = forceMode;
    _invert = invert;
    _mode = CaliperMode::DETECTING;
    _on = false;
    _pinClk = PIN_CALIPER_CLK;    // la auto-detección puede intercambiarlos
    _pinData = PIN_CALIPER_DATA;

    switch (forceMode) {
        case CaliperMode::DIGITAL: startDigital(); break;
        case CaliperMode::ADC:     startAdc();     break;
        default:
            xTaskCreate(detectTask, "cal_detect", 3072, this, 1, &_detectTaskHandle);
            break;
    }
}

void Caliper::startDigital()
{
    stopReaders();
    _isrBits = 0;
    _isrShift = 0;
    _isrLastEdgeUs = 0;
    _mode = CaliperMode::DIGITAL;
    gpio_set_intr_type((gpio_num_t)_pinClk, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add((gpio_num_t)_pinClk, clkIsr, this);
    _isrAttached = true;
}

void Caliper::startAdc()
{
    stopReaders();
    _mode = CaliperMode::ADC;
    _lastAdcFrameMs = 0;
    xTaskCreate(adcTask, "cal_adc", 3072, this, CALIPER_ADC_TASK_PRIO, &_adcTaskHandle);
}

void Caliper::stopReaders()
{
    if (_isrAttached) {
        gpio_isr_handler_remove((gpio_num_t)_pinClk);
        _isrAttached = false;
    }
    if (_adcTaskHandle) { vTaskDelete(_adcTaskHandle); _adcTaskHandle = nullptr; }
    _mode = CaliperMode::DETECTING;
}

void Caliper::end()
{
    if (_detectTaskHandle) { vTaskDelete(_detectTaskHandle); _detectTaskHandle = nullptr; }
    stopReaders();
}

// Seguro desde cualquier tarea: solo marca el pedido; poll() (tarea del loop)
// ejecuta la transición, evitando vTaskDelete/detach concurrentes con poll().
void Caliper::redetect()
{
    _redetectRequested = true;
}

// Decodifica un frame BIN6 de 24 bits. Layout verificado (Yuriy's Toys,
// wei48221): [0..19] valor, [20] signo, [23] pulgadas. mm=c/100, inch=c/2000.
bool Caliper::decode24(uint32_t packet, CaliperReading& out)
{
    int32_t counts = (int32_t)(packet & 0xFFFFF);
    if (packet & 0x100000) counts = -counts;
    bool inch = (packet & 0x800000) != 0;

    float mm = inch ? (counts / 2000.0f) * 25.4f : counts / 100.0f;

    // Validación: un calibre de 150-300 mm no debería pasar de ±400 mm.
    if (mm < -400.0f || mm > 400.0f) return false;

    out.counts = counts;
    out.value_mm = mm;
    out.unit = inch ? CaliperUnit::INCH : CaliperUnit::MM;
    out.timestamp = millis();
    out.raw = packet;
    return true;
}

bool Caliper::handleFrame(const Frame& f, CaliperReading& out)
{
    // los campos de 64 bits se leen desde otras tareas (diag): copiar bajo mux
    portENTER_CRITICAL(&_mux);
    _lastFrameBits = f.count;
    _lastFrameRaw = f.bits;
    portEXIT_CRITICAL(&_mux);

    if (f.count == CALIPER_PACKET_BITS && decode24((uint32_t)f.bits, out)) {
        _framesOk = _framesOk + 1;
        return true;
    }
    // 21 bits = iGaging, 48 = Sylvac 2x24: variantes sin validar; se cuentan
    // como "bad" y quedan visibles en diag() para analizarlas si aparecen.
    _framesBad = _framesBad + 1;
    return false;
}

bool Caliper::poll(CaliperReading& out)
{
    // Re-detección pedida desde web/serial: aplicarla acá, en la tarea del
    // loop, para que stopReaders/startX nunca se solapen con poll().
    if (_redetectRequested) {
        _redetectRequested = false;
        end();
        begin(_forceMode, _invert);
    }

    if (!_queue) return false;

    // Detección de apagado: sin paquetes por CALIPER_IDLE_OFF_MS
    if (_on && (esp_timer_get_time() - _lastPacketUs) > (int64_t)CALIPER_IDLE_OFF_MS * 1000) {
        _on = false;
    }

    bool got = false;

    // Modo digital: finalizar el frame pendiente apenas vence el gap, sin
    // esperar al primer flanco del próximo paquete (~100 ms después).
    if (_mode == CaliperMode::DIGITAL && _isrBits > 0) {
        Frame f = {};
        bool pending = false;
        portENTER_CRITICAL(&_mux);
        if (_isrBits > 0 && esp_timer_get_time() - _isrLastEdgeUs > CALIPER_BIT_GAP_US) {
            f.bits = _isrShift;
            f.count = _isrBits;
            _isrShift = 0;
            _isrBits = 0;
            pending = true;
        }
        portEXIT_CRITICAL(&_mux);
        if (pending && handleFrame(f, out)) got = true;
    }

    // Drenar la cola (frames cerrados por el ISR o la tarea ADC); quedarse
    // con la última lectura válida.
    Frame f;
    while (xQueueReceive(_queue, &f, 0) == pdTRUE) {
        CaliperReading r;
        if (handleFrame(f, r)) {
            out = r;
            got = true;
        }
    }

    if (got) {
        portENTER_CRITICAL(&_mux);   // _last se lee desde la tarea async (web)
        _last = out;
        portEXIT_CRITICAL(&_mux);
        _hasReading = true;
        _lastPacketUs = esp_timer_get_time();
        _on = true;
    }
    return got;
}

CaliperReading Caliper::lastAtomic()
{
    CaliperReading r;
    portENTER_CRITICAL(&_mux);
    r = _last;
    portEXIT_CRITICAL(&_mux);
    return r;
}

CaliperDiag Caliper::diag()
{
    CaliperDiag d;
    d.mode = _mode;
    d.on = _on;
    d.framesOk = _framesOk;
    d.framesBad = _framesBad;
    portENTER_CRITICAL(&_mux);   // campos de 64 bits: copiar bajo mux
    d.lastFrameBits = _lastFrameBits;
    d.lastFrameRaw = _lastFrameRaw;
    portEXIT_CRITICAL(&_mux);
    // niveles instantáneos (fuera del mux: el ADC bloquea) — con suerte CLK
    // se ve en reposo (alto). Usa el mapeo de pines ya detectado.
    d.clkMv = adcReadMv(_pinClk);
    d.dataMv = adcReadMv(_pinData);
    return d;
}
