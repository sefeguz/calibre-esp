// Plantillas de medición para diseñar cajitas de dispositivos IoT/Arduino.
//
// Filosofía: medir lo FÁCIL con el calibre (bordes, distancias a lados) y
// derivar lo difícil (centros de agujeros) en el CAD. Por eso muchos ítems
// piden "borde a centro" o "separacion (centros)" en vez de centro-a-centro
// directo, que es incómodo de medir.
//
// Datum sugerido: esquina inferior-izquierda mirando el lado de componentes.
// Nombres en ASCII (sin acentos) para evitar problemas de encoding en el
// código fuente; máximo SESSION_NAME_LEN (40) caracteres c/u.
#pragma once

#include <Arduino.h>

struct MeasureTemplate {
    const char*        id;
    const char*        name;
    const char* const* items;
    uint8_t            count;
};

// --- Caja simple: solo el contorno y el stack en Z (cajita rápida) ---
static const char* const TPL_SIMPLE[] = {
    "Largo placa",
    "Ancho placa",
    "Espesor PCB",
    "Alto sobre PCB (comp mas alto)",
    "Alto bajo PCB (pines)",
};

// --- Dev board (ESP/Arduino) con USB y 4 agujeros simetricos ---
static const char* const TPL_DEVBOARD[] = {
    "Largo placa",
    "Ancho placa",
    "Espesor PCB",
    "Alto sobre PCB (comp mas alto)",
    "Alto bajo PCB (pines)",
    "USB: ancho abertura",
    "USB: alto abertura",
    "USB: centro a borde lateral",
    "USB: centro sobre PCB",
    "Agujeros: diametro",
    "Agujeros: separacion X (centros)",
    "Agujeros: separacion Y (centros)",
    "Agujeros: borde a centro X",
    "Agujeros: borde a centro Y",
};

// --- Placa + display (OLED/LCD): suma la ventana visible del display ---
static const char* const TPL_DISPLAY[] = {
    "Largo placa",
    "Ancho placa",
    "Espesor PCB",
    "Alto bajo PCB (pines)",
    "USB: ancho abertura",
    "USB: alto abertura",
    "USB: centro a borde lateral",
    "USB: centro sobre PCB",
    "Display: alto modulo total",
    "Display: ventana visible largo",
    "Display: ventana visible ancho",
    "Display: borde sup a ventana",
    "Display: borde izq a ventana",
    "Agujeros: diametro",
    "Agujeros: separacion X (centros)",
    "Agujeros: separacion Y (centros)",
    "Agujeros: borde a centro X",
    "Agujeros: borde a centro Y",
};

// --- Modulo / sensor breakout (chico, con header de pines) ---
static const char* const TPL_SENSOR[] = {
    "Largo placa",
    "Ancho placa",
    "Espesor PCB",
    "Alto sobre PCB (comp mas alto)",
    "Alto bajo PCB (pines/headers)",
    "Header: largo fila de pines",
    "Header: centro a borde",
    "Agujeros: diametro",
    "Agujeros: borde a centro X",
    "Agujeros: borde a centro Y",
};

// --- Panel con botones y LEDs (cara frontal) ---
static const char* const TPL_PANEL[] = {
    "Largo placa",
    "Ancho placa",
    "Espesor PCB",
    "Alto sobre PCB (comp mas alto)",
    "Boton 1: centro X desde borde",
    "Boton 1: centro Y desde borde",
    "Boton 1: diametro cap",
    "Boton 1: alto sobre PCB",
    "Boton 2: centro X desde borde",
    "Boton 2: centro Y desde borde",
    "LED: centro X desde borde",
    "LED: centro Y desde borde",
    "Agujeros: diametro",
    "Agujeros: borde a centro X",
    "Agujeros: borde a centro Y",
};

#define TPL(arr) (arr), (uint8_t)(sizeof(arr) / sizeof((arr)[0]))

static const MeasureTemplate MEASURE_TEMPLATES[] = {
    { "simple",   "Caja simple (contorno)",   TPL(TPL_SIMPLE) },
    { "devboard", "Dev board (ESP/Arduino)",  TPL(TPL_DEVBOARD) },
    { "display",  "Placa + display (OLED/LCD)", TPL(TPL_DISPLAY) },
    { "sensor",   "Modulo / sensor breakout", TPL(TPL_SENSOR) },
    { "panel",    "Con botones y LEDs",       TPL(TPL_PANEL) },
};

#undef TPL

static const uint8_t MEASURE_TEMPLATES_COUNT =
    sizeof(MEASURE_TEMPLATES) / sizeof(MEASURE_TEMPLATES[0]);

// Busca una plantilla por id; nullptr si no existe.
static inline const MeasureTemplate* templateById(const char* id)
{
    if (!id) return nullptr;
    for (uint8_t i = 0; i < MEASURE_TEMPLATES_COUNT; i++) {
        if (strcmp(MEASURE_TEMPLATES[i].id, id) == 0) return &MEASURE_TEMPLATES[i];
    }
    return nullptr;
}
