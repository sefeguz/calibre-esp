#include "caliper.h"
#include "config.h"

#include <esp_timer.h>
#include <driver/gpio.h>

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

    int clk = gpio_get_level((gpio_num_t)PIN_CALIPER_CLK);
    int data = gpio_get_level((gpio_num_t)PIN_CALIPER_DATA);
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
    while (analogReadMilliVolts(PIN_CALIPER_CLK) > CALIPER_ADC_THRESH_MV) {
        if (millis() > bitDeadlineMs) return -1;
    }
    while (analogReadMilliVolts(PIN_CALIPER_CLK) < CALIPER_ADC_THRESH_MV) {
        if (millis() > bitDeadlineMs) return -1;
    }
    return analogReadMilliVolts(PIN_CALIPER_DATA) > CALIPER_ADC_THRESH_MV ? 1 : 0;
}

void Caliper::adcTask(void* arg)
{
    Caliper* self = static_cast<Caliper*>(arg);
    uint32_t interFrameMs = 0;  // intervalo entre paquetes, medido en caliente

    for (;;) {
        // ceder CPU: lo mínimo 1 tick; si conocemos el ritmo, dormir el 60% del gap
        if (interFrameMs >= 40) {
            vTaskDelay(pdMS_TO_TICKS(interFrameMs * 6 / 10));
        } else {
            vTaskDelay(1);
        }

        uint32_t packet = 0;
        int bitIndex = 0;
        bool ok = false;
        uint32_t packetDeadline = millis() + 300;

        while (millis() < packetDeadline) {
            int bit = adcReadBit(millis() + 30);
            if (bit < 0) {            // gap/timeout: frame trunco, reintentar
                bitIndex = 0;
                packet = 0;
                continue;
            }
            packet |= ((uint32_t)bit) << bitIndex;
            if (++bitIndex == CALIPER_PACKET_BITS) { ok = true; break; }
        }

        if (ok) {
            Frame f = { packet, CALIPER_PACKET_BITS };
            xQueueSend(self->_queue, &f, 0);

            uint32_t nowMs = millis();
            if (self->_lastAdcFrameMs) {
                uint32_t d = nowMs - self->_lastAdcFrameMs;
                if (d > 20 && d < 2000) interFrameMs = d;
            }
            self->_lastAdcFrameMs = nowMs;
        } else {
            interFrameMs = 0;                  // sin señal: olvidar el ritmo
            vTaskDelay(pdMS_TO_TICKS(50));     // y dormir bastante
        }
    }
}

// ---------------------------------------------------------------------------
// Auto-detección: primero flancos digitales (señal 3 V) en una ventana mayor
// al gap entre paquetes (~100-300 ms); si no hay, actividad analógica (1.5 V).
// Reintenta hasta encontrar señal (el calibre puede estar apagado).
// ---------------------------------------------------------------------------
static volatile uint32_t s_detectEdges = 0;

void IRAM_ATTR Caliper::detectIsr(void*) { s_detectEdges = s_detectEdges + 1; }

void Caliper::detectTask(void* arg)
{
    Caliper* self = static_cast<Caliper*>(arg);

    for (;;) {
        // 1) ¿flancos digitales? (señal llega al VIH ~2.47 V)
        s_detectEdges = 0;
        gpio_set_intr_type((gpio_num_t)PIN_CALIPER_CLK, GPIO_INTR_ANYEDGE);
        gpio_isr_handler_add((gpio_num_t)PIN_CALIPER_CLK, detectIsr, nullptr);
        vTaskDelay(pdMS_TO_TICKS(CALIPER_DETECT_MS));
        gpio_isr_handler_remove((gpio_num_t)PIN_CALIPER_CLK);

        if (s_detectEdges >= 2 * CALIPER_PACKET_BITS - 8) {  // ~1 frame (2 flancos/bit)
            self->_detectTaskHandle = nullptr;
            self->startDigital();
            vTaskDelete(nullptr);
            return;
        }

        // 2) ¿actividad analógica? (señal de ~1.5 V)
        uint32_t deadline = millis() + CALIPER_DETECT_MS;
        uint32_t maxMv = 0, minMv = UINT32_MAX;
        while (millis() < deadline) {
            uint32_t mv = analogReadMilliVolts(PIN_CALIPER_CLK);
            if (mv > maxMv) maxMv = mv;
            if (mv < minMv) minMv = mv;
            if ((deadline - millis()) % 50 == 0) vTaskDelay(1);  // ceder CPU
        }
        // CLK reposa alto y pulsa a bajo: hay señal si el nivel alto supera el
        // umbral y se vieron transiciones (min bien por debajo del max)
        if (maxMv > CALIPER_ADC_THRESH_MV && minMv < CALIPER_ADC_THRESH_MV / 2) {
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

    // Servicio de ISR GPIO en IRAM: sobrevive a escrituras de flash (NVS/OTA).
    // ESP_ERR_INVALID_STATE = ya instalado, OK.
    esp_err_t err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        log_e("gpio_install_isr_service: %d", err);
    }

    if (!_queue) _queue = xQueueCreate(8, sizeof(Frame));
    _forceMode = forceMode;
    _invert = invert;
    _mode = CaliperMode::DETECTING;
    _on = false;

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
    gpio_set_intr_type((gpio_num_t)PIN_CALIPER_CLK, GPIO_INTR_ANYEDGE);
    gpio_isr_handler_add((gpio_num_t)PIN_CALIPER_CLK, clkIsr, this);
    _isrAttached = true;
}

void Caliper::startAdc()
{
    stopReaders();
    _mode = CaliperMode::ADC;
    _lastAdcFrameMs = 0;
    xTaskCreate(adcTask, "cal_adc", 3072, this, 2, &_adcTaskHandle);
}

void Caliper::stopReaders()
{
    if (_isrAttached) {
        gpio_isr_handler_remove((gpio_num_t)PIN_CALIPER_CLK);
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
    // se ve en reposo (alto)
    d.clkMv = analogReadMilliVolts(PIN_CALIPER_CLK);
    d.dataMv = analogReadMilliVolts(PIN_CALIPER_DATA);
    return d;
}
