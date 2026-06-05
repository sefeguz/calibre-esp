#include "caliper.h"
#include "config.h"

#include <esp_timer.h>
#include <driver/gpio.h>
#include <esp_adc/adc_continuous.h>

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
// Modo ADC por DMA (adc_continuous) — para señales de ~1.5 V.
//
// El polling con analogRead (EspDRO) funciona, pero en el C3 single-core
// cualquier interrupción de WiFi/BLE de >100 µs durante la ráfaga rompe ese
// paquete (~50% de pérdida con la radio activa). El modo continuo del ADC
// muestrea por HARDWARE a 80 kS/s (40 kS/s por canal, ~6 muestras por fase
// del clock más lento) sin importar qué hace la CPU; la tarea después
// decodifica la forma de onda completa con histéresis. Captura ~100% de los
// paquetes bajo cualquier carga, y DATA queda muestreado a ≤25 µs del flanco
// (adiós glitches del bit 23/inch por muestreo tardío).
// ---------------------------------------------------------------------------
#define DMA_SAMPLE_FREQ_HZ   80000   // total (2 canales -> 40 kS/s c/u)
#define DMA_FRAME_BYTES      256
#define DMA_POOL_BYTES       8192    // ~25 ms de margen si la CPU se atrasa
#define DMA_THRESH_HIGH_RAW  1400    // histéresis (1.5 V ~ 2400 cuentas)
#define DMA_THRESH_LOW_RAW   800
#define DMA_GAP_SAMPLES      80      // ~2 ms sin flancos de CLK => fin de frame

void Caliper::dmaTask(void* arg)
{
    Caliper* self = static_cast<Caliper*>(arg);
    adc_continuous_handle_t handle = (adc_continuous_handle_t)self->_dmaHandle;

    adc_channel_t clkCh, dataCh;
    adc_unit_t unit;
    adc_continuous_io_to_channel(self->_pinClk, &unit, &clkCh);
    adc_continuous_io_to_channel(self->_pinData, &unit, &dataCh);

    uint8_t buf[DMA_FRAME_BYTES];
    bool clkHigh = false, dataHigh = false;
    uint64_t shift = 0;
    uint8_t bits = 0;
    uint32_t clkSamplesSinceEdge = 0;

    while (!self->_dmaStop) {
        uint32_t got = 0;
        esp_err_t err = adc_continuous_read(handle, buf, sizeof(buf), &got, 50);
        if (err != ESP_OK) continue;   // timeout: sin datos (no debería pasar)

        for (uint32_t i = 0; i + sizeof(adc_digi_output_data_t) <= got;
             i += sizeof(adc_digi_output_data_t)) {
            adc_digi_output_data_t* p = (adc_digi_output_data_t*)&buf[i];
            uint32_t ch = p->type2.channel;
            uint32_t raw = p->type2.data;

            if (ch == (uint32_t)dataCh) {
                // histéresis para no castañetear cerca del umbral
                dataHigh = dataHigh ? (raw > DMA_THRESH_LOW_RAW)
                                    : (raw > DMA_THRESH_HIGH_RAW);
                self->_lastDataRaw = raw;
                continue;
            }
            if (ch != (uint32_t)clkCh) continue;

            self->_lastClkRaw = raw;
            clkSamplesSinceEdge++;

            // gap sin flancos => el frame terminó: encolarlo con su conteo
            if (bits > 0 && clkSamplesSinceEdge > DMA_GAP_SAMPLES) {
                Frame f = { shift, bits };
                xQueueSend(self->_queue, &f, 0);
                shift = 0;
                bits = 0;
                self->adcWindows = self->adcWindows + 1;
            }

            bool nh = clkHigh ? (raw > DMA_THRESH_LOW_RAW)
                              : (raw > DMA_THRESH_HIGH_RAW);
            if (nh != clkHigh) {
                clkHigh = nh;
                clkSamplesSinceEdge = 0;
                if (nh) {  // flanco ascendente de CLK: el dato es válido
                    if (bits < 60) {
                        shift |= ((uint64_t)(dataHigh ? 1 : 0)) << bits;
                        bits++;
                        if (bits > self->adcMaxBits) self->adcMaxBits = bits;
                    }
                }
            }
        }
    }

    self->_adcTaskHandle = nullptr;
    vTaskDelete(nullptr);
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

    adc_channel_t clkCh, dataCh;
    adc_unit_t unit;
    if (adc_continuous_io_to_channel(_pinClk, &unit, &clkCh) != ESP_OK ||
        adc_continuous_io_to_channel(_pinData, &unit, &dataCh) != ESP_OK) {
        log_e("pines sin canal ADC");
        return;
    }

    adc_continuous_handle_t handle = nullptr;
    adc_continuous_handle_cfg_t hcfg = {};
    hcfg.max_store_buf_size = DMA_POOL_BYTES;
    hcfg.conv_frame_size = DMA_FRAME_BYTES;
    if (adc_continuous_new_handle(&hcfg, &handle) != ESP_OK) {
        log_e("adc_continuous_new_handle fallo");
        return;
    }

    adc_digi_pattern_config_t pattern[2] = {};
    pattern[0].atten = ADC_ATTEN_DB_12;
    pattern[0].channel = clkCh;
    pattern[0].unit = ADC_UNIT_1;
    pattern[0].bit_width = 12;
    pattern[1] = pattern[0];
    pattern[1].channel = dataCh;

    adc_continuous_config_t ccfg = {};
    ccfg.pattern_num = 2;
    ccfg.adc_pattern = pattern;
    ccfg.sample_freq_hz = DMA_SAMPLE_FREQ_HZ;
    ccfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
    ccfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
    if (adc_continuous_config(handle, &ccfg) != ESP_OK ||
        adc_continuous_start(handle) != ESP_OK) {
        log_e("adc_continuous config/start fallo");
        adc_continuous_deinit(handle);
        return;
    }

    _dmaHandle = handle;
    _dmaStop = false;
    _mode = CaliperMode::ADC;
    xTaskCreate(dmaTask, "cal_dma", 3072, this, CALIPER_ADC_TASK_PRIO, &_adcTaskHandle);
}

void Caliper::stopReaders()
{
    if (_isrAttached) {
        gpio_isr_handler_remove((gpio_num_t)_pinClk);
        _isrAttached = false;
    }
    if (_adcTaskHandle) {
        // pedirle a la tarea que termine (matarla a mitad de una lectura del
        // driver dejaría locks del kernel tomados) y recién después apagar
        _dmaStop = true;
        while (_adcTaskHandle) vTaskDelay(1);
    }
    if (_dmaHandle) {
        adc_continuous_stop((adc_continuous_handle_t)_dmaHandle);
        adc_continuous_deinit((adc_continuous_handle_t)_dmaHandle);
        _dmaHandle = nullptr;
    }
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

// Filtro anti-glitch: un frame con bits corruptos (WiFi/BLE recortan ~4% de
// los paquetes) a veces pasa la validación de rango y muestra un valor falso
// por un instante. Los cambios chicos pasan directo (cero latencia para
// medir); un salto > 2 mm queda pendiente y se acepta recién si el frame
// siguiente también salta en la MISMA dirección (movimiento real: todos los
// frames barren para el mismo lado; glitch: valor aislado al azar).
bool Caliper::acceptReading(const CaliperReading& r)
{
    if (!_hasReading || !_on) {        // primera lectura o calibre recién prendido
        _jumpPending = false;
        _unitPending = false;
        return true;
    }

    // cambio de unidad (bit 23): confirmar con un segundo frame igual
    if (r.unit != _last.unit) {
        if (!(_unitPending && r.unit == _pendingUnit)) {
            _unitPending = true;
            _pendingUnit = r.unit;
            return false;
        }
        _unitPending = false;          // confirmado: sigue al chequeo de salto
    } else {
        _unitPending = false;
    }

    float delta = r.value_mm - _last.value_mm;
    if (fabsf(delta) <= 2.0f) {
        _jumpPending = false;
        return true;
    }

    if (_jumpPending) {
        // segundo salto consecutivo: ¿misma dirección que el pendiente?
        float prevDelta = _jumpFromMm;
        _jumpPending = false;
        if ((delta > 0) == (prevDelta > 0)) return true;   // barrido real
        return false;                                       // glitches sueltos
    }

    _jumpPending = true;       // retener un frame (~112 ms) para confirmar
    _jumpFromMm = delta;
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
        if (pending && handleFrame(f, out) && acceptReading(out)) got = true;
    }

    // Drenar la cola (frames cerrados por el ISR o la tarea ADC); quedarse
    // con la última lectura válida que pase el filtro anti-glitch.
    Frame f;
    while (xQueueReceive(_queue, &f, 0) == pdTRUE) {
        CaliperReading r;
        if (handleFrame(f, r) && acceptReading(r)) {
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
    // Niveles instantáneos. En modo ADC-DMA el driver continuo es dueño del
    // ADC (una lectura oneshot fallaría): usar los niveles que ya vio el DMA.
    if (_mode == CaliperMode::ADC) {
        d.clkMv = (uint32_t)_lastClkRaw * 2500 / 4095;
        d.dataMv = (uint32_t)_lastDataRaw * 2500 / 4095;
    } else {
        d.clkMv = adcReadMv(_pinClk);
        d.dataMv = adcReadMv(_pinData);
    }
    return d;
}
