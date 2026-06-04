// Estado compartido: instancia del calibre, zero relativo y log de capturas.
#include "app.h"
#include "config.h"
#include "settings.h"
#include "ble_keyboard.h"

Caliper caliper;

static volatile bool s_relActive = false;
static float s_relOffsetMm = 0.0f;

// Ring de capturas: muta solo en loop(); se lee también desde la tarea async
// (web) -> accesos bajo mux para no ver head/count inconsistentes.
static portMUX_TYPE s_capMux = portMUX_INITIALIZER_UNLOCKED;
static Capture s_captures[CAPTURES_SIZE];
static size_t  s_capHead = 0;   // próxima posición de escritura
static size_t  s_capCount = 0;

// pedidos diferidos desde handlers async hacia loop()
static volatile bool s_capReq = false;
static volatile bool s_zeroReq = false;
static volatile bool s_clearReq = false;

float appDisplayedMmFrom(const CaliperReading& r)
{
    return s_relActive ? r.value_mm - s_relOffsetMm : r.value_mm;
}

float appDisplayedMm()
{
    return appDisplayedMmFrom(caliper.lastAtomic());
}

bool appRelativeActive() { return s_relActive; }

void appToggleRelative()
{
    if (s_relActive) {
        s_relActive = false;
        s_relOffsetMm = 0.0f;
    } else if (caliper.hasReading() && caliper.isOn()) {
        s_relOffsetMm = caliper.lastAtomic().value_mm;
        s_relActive = true;
    }
}

bool appCapture()
{
    if (!caliper.hasReading() || !caliper.isOn()) return false;

    CaliperReading r = caliper.lastAtomic();
    float mm = appDisplayedMmFrom(r);

    portENTER_CRITICAL(&s_capMux);
    s_captures[s_capHead] = { mm, millis() };
    s_capHead = (s_capHead + 1) % CAPTURES_SIZE;
    if (s_capCount < CAPTURES_SIZE) s_capCount++;
    portEXIT_CRITICAL(&s_capMux);

    // tipear por BLE si hay host listo (en la unidad que muestra el LCD).
    // Corre en loop(): los delay() del tipeo no bloquean la tarea de red.
    if (settings.bleEnabled && bleKeyboard.isConnected()) {
        bool inch = r.unit == CaliperUnit::INCH;
        float value = inch ? mm / 25.4f : mm;
        bleKeyboard.typeMeasurement(value, inch ? 4 : 2, settings.decimalSep,
                                    settings.eolKey);
    }
    return true;
}

void appClearCaptures()
{
    portENTER_CRITICAL(&s_capMux);
    s_capCount = 0;
    s_capHead = 0;
    portEXIT_CRITICAL(&s_capMux);
}

// --- pedidos diferidos ---

void appRequestCapture()       { s_capReq = true; }
void appRequestZero()          { s_zeroReq = true; }
void appRequestClearCaptures() { s_clearReq = true; }

static bool consume(volatile bool& flag)
{
    if (!flag) return false;
    flag = false;
    return true;
}

bool appConsumeCaptureRequest() { return consume(s_capReq); }
bool appConsumeZeroRequest()    { return consume(s_zeroReq); }
bool appConsumeClearRequest()   { return consume(s_clearReq); }

// --- log de capturas ---

size_t appCaptureCount()
{
    portENTER_CRITICAL(&s_capMux);
    size_t n = s_capCount;
    portEXIT_CRITICAL(&s_capMux);
    return n;
}

Capture appCaptureAt(size_t i)
{
    portENTER_CRITICAL(&s_capMux);
    size_t start = (s_capHead + CAPTURES_SIZE - s_capCount) % CAPTURES_SIZE;
    Capture c = s_captures[(start + i) % CAPTURES_SIZE];
    portEXIT_CRITICAL(&s_capMux);
    return c;
}
