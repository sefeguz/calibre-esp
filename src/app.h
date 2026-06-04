// Estado y acciones compartidas entre main, webserver, botón y BLE.
//
// Concurrencia: los handlers HTTP corren en la tarea AsyncTCP (prioridad 10),
// NO en el loop. Las acciones que mutan estado (capturar, zero, borrar) se
// piden con appRequest*() desde cualquier tarea y se ejecutan en loop() vía
// appConsume*(). Las lecturas (appCaptureAt/Count, appDisplayedMmFrom) son
// seguras desde cualquier tarea (protegidas por mux o de palabra única).
#pragma once

#include <Arduino.h>
#include "caliper.h"

struct Capture {
    float    value_mm;   // valor mostrado (ya con offset relativo aplicado)
    uint32_t ts;         // millis() de la captura
};

extern Caliper caliper;

// Valor mostrado = lectura - offset relativo (zero rel)
float appDisplayedMm();
float appDisplayedMmFrom(const CaliperReading& r);  // desde un snapshot
bool  appRelativeActive();

// --- Solo desde loop() ---
void appToggleRelative();
bool appCapture();          // guarda en el log y tipea por BLE; false si no hay lectura
void appClearCaptures();

// --- Pedidos diferidos (seguros desde handlers async) ---
void appRequestCapture();
void appRequestZero();
void appRequestClearCaptures();
bool appConsumeCaptureRequest();   // leer-y-limpiar, llamar desde loop()
bool appConsumeZeroRequest();
bool appConsumeClearRequest();

// Acceso al log de capturas (lecturas consistentes, cualquier tarea)
size_t  appCaptureCount();
Capture appCaptureAt(size_t i);    // i=0 es la más vieja (copia bajo mux)
