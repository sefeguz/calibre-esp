// Lector del puerto SPC de calibres digitales chinos (protocolo BIN6, 24 bits).
//
// Dos modos con auto-detección (ninguno requiere electrónica adicional):
//  - DIGITAL: ISR en ambos flancos de CLK (ANYEDGE); el bit de DATA se acumula
//    solo cuando CLK queda ALTO (flanco ascendente efectivo) — igual que las
//    dos referencias probadas en hardware (jkent/esp32-caliper y EspDRO).
//    Con `invert` (level-shifter NPN) se niegan CLK y DATA en software.
//    Framing por gap (>1 ms sin flancos cierra el frame con su conteo de bits).
//    Registrado vía IDF con ESP_INTR_FLAG_IRAM para sobrevivir escrituras de
//    flash (NVS/OTA) sin crash. Requiere señal que alcance VIH ≈ 2.47 V.
//  - ADC: polling con umbral por software para señales de ~1.5 V que no
//    alcanzan el VIH digital (MGX3D/EspDRO). Limitación: solo calibres de
//    clock lento (<~4 kHz); los de ráfaga ~90 kHz a 1.5 V requieren el
//    level-shifter NPN + modo digital invertido.
//
// Decodificación BIN6 (LSB primero): bits[0..19] valor, bit20 signo,
// bit23 bandera pulgadas. mm = cuentas/100; inch = cuentas/2000.
// Frames con otro número de bits (21=iGaging, 48=Sylvac 2x24) se cuentan y
// exponen en diagnóstico, no se decodifican (validar antes de soportar).
//
// Concurrencia: poll(), begin() y end() deben llamarse SOLO desde la tarea
// del loop. redetect() y diag() son seguras desde cualquier tarea (redetect
// solo marca un flag que poll() procesa; diag copia bajo critical section).
#pragma once

#include <Arduino.h>
#include "config.h"

enum class CaliperMode : uint8_t {
    DETECTING = 0,  // buscando señal
    DIGITAL,        // ISR digital (señal ~3 V)
    ADC,            // polling ADC (señal ~1.5 V)
};

enum class CaliperUnit : uint8_t { MM = 0, INCH = 1 };

struct CaliperReading {
    int32_t  counts;     // valor crudo con signo (cuentas del calibre)
    float    value_mm;   // siempre en mm (independiente de la unidad del LCD)
    CaliperUnit unit;    // unidad que muestra el LCD del calibre
    uint32_t timestamp;  // millis()
    uint32_t raw;        // paquete crudo de 24 bits (debug)
};

struct CaliperDiag {
    CaliperMode mode;
    bool     on;
    uint32_t framesOk;
    uint32_t framesBad;      // descartados (validación o variante no soportada)
    uint8_t  lastFrameBits;  // bits del último frame (24 esperado; 21/48=variante)
    uint64_t lastFrameRaw;
    uint32_t clkMv;          // nivel instantáneo (ADC) — útil sin osciloscopio
    uint32_t dataMv;
};

class Caliper {
public:
    // forceMode: DETECTING = auto (default), o forzar DIGITAL/ADC.
    // invert: señales invertidas por level-shifter NPN (solo modo digital).
    void begin(CaliperMode forceMode = CaliperMode::DETECTING, bool invert = false);
    void end();

    // Llamar desde loop(). Devuelve true si hay lectura nueva en `out`.
    bool poll(CaliperReading& out);

    bool        isOn() const { return _on; }
    CaliperMode mode() const { return _mode; }
    bool        hasReading() const { return _hasReading; }

    // Copia consistente de la última lectura (segura desde cualquier tarea).
    CaliperReading lastAtomic();

    void redetect();          // pide re-detección; poll() la aplica (thread-safe)
    CaliperDiag diag();       // estado para web/serial (lee ADC bajo demanda)

    uint8_t pinClk() const { return _pinClk; }
    uint8_t pinData() const { return _pinData; }
    bool    pinsSwapped() const { return _pinClk != PIN_CALIPER_CLK; }

private:
    struct Frame { uint64_t bits; uint8_t count; };

    static void IRAM_ATTR clkIsr(void* arg);
    static void dmaTask(void* arg);
    static void detectTask(void* arg);

    void startDigital();
    void startAdc();
    void stopReaders();
    bool acceptReading(CaliperReading& r);   // mediana de 3 (modifica r)
    bool handleFrame(const Frame& f, CaliperReading& out);
    bool decode24(uint32_t packet, CaliperReading& out);

    // estado ISR (modo digital)
    volatile uint64_t _isrShift = 0;
    volatile uint8_t  _isrBits = 0;
    volatile int64_t  _isrLastEdgeUs = 0;
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;

    QueueHandle_t _queue = nullptr;
    TaskHandle_t  _adcTaskHandle = nullptr;
    TaskHandle_t  _detectTaskHandle = nullptr;
    bool          _isrAttached = false;

    // modo ADC por DMA (adc_continuous): el hardware muestrea a 80 kS/s sin
    // importar las interrupciones de WiFi/BLE; la tarea decodifica la onda
    void* _dmaHandle = nullptr;            // adc_continuous_handle_t
    volatile bool _dmaStop = false;
    volatile uint16_t _lastClkRaw = 0;     // niveles vistos por el DMA (diag)
    volatile uint16_t _lastDataRaw = 0;

    CaliperMode _mode = CaliperMode::DETECTING;
    CaliperMode _forceMode = CaliperMode::DETECTING;
    bool _invert = false;

    // Filtro anti-glitch por MEDIANA DE 3: salida a tasa completa (un frame
    // de salida por frame de entrada, lag fijo de 1 frame ~112 ms) y un
    // glitch aislado nunca es la mediana. Sin penalidad de velocidad.
    CaliperReading _hist[3];
    uint8_t _histCount = 0;
    uint8_t _histIdx = 0;
    // Algunos calibres traen el conector con DATA y CLK al revés: la
    // auto-detección identifica el CLK real (el que más conmuta) y ajusta.
    uint8_t _pinClk = PIN_CALIPER_CLK;
    uint8_t _pinData = PIN_CALIPER_DATA;
    volatile bool _on = false;
    volatile bool _redetectRequested = false;
    bool _hasReading = false;
    CaliperReading _last = {};
    int64_t _lastPacketUs = 0;

    // diagnóstico (lecturas de 64 bits protegidas por _mux)
    volatile uint32_t _framesOk = 0;
    volatile uint32_t _framesBad = 0;
    volatile uint8_t  _lastFrameBits = 0;
    volatile uint64_t _lastFrameRaw = 0;

public:
    // instrumentación del modo ADC (debug de bring-up)
    volatile uint8_t  adcMaxBits = 0;    // máximo de bits juntados en un intento
    volatile uint32_t adcBitTimeouts = 0;
    volatile uint32_t adcWindows = 0;    // ventanas de caza completadas
};
