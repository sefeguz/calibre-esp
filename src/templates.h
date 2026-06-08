// Plantillas de medición para diseñar cajitas de dispositivos IoT/Arduino.
//
// Filosofía: medir lo FÁCIL con el calibre (bordes, caras planas) y derivar
// lo difícil en el CAD. Dos consecuencias en los nombres:
//  - Las ALTURAS se miden incluyendo el PCB (contra su cara plana, que es
//    fácil de apoyar) y el diseño resta el espesor del PCB:
//      alto_real_arriba = "alto total arriba" - "espesor PCB"
//      alto_real_abajo  = "alto total abajo"  - "espesor PCB"
//  - Los AGUJEROS se miden por sus BORDES (no por el centro, que no se puede
//    apoyar):
//      centro-a-centro = "span exterior" - "diametro"   (agujeros iguales)
//      centro (desde el borde de la placa) = "borde placa a borde" + diametro/2
//  - Conectores: posicion horizontal = "borde placa a borde USB" + ancho/2;
//    el recorte vertical va de "cara inf PCB a base USB" hacia arriba "alto".
//
// Cada plantilla incluye las companeras necesarias para cerrar la derivacion
// (espesor PCB, diametro de agujero, ancho de abertura).
//
// Datum sugerido: esquina inferior-izquierda mirando el lado de componentes.
// Nombres en ASCII (sin acentos) por el encoding del fuente; max 40 chars.
#pragma once

#include <Arduino.h>

struct MeasureTemplate {
    const char*        id;
    const char*        name;
    const char* const* items;
    uint8_t            count;
};

// --- Caja simple: contorno + stack en Z (cajita rápida) ---
static const char* const TPL_SIMPLE[] = {
    "Largo placa",
    "Ancho placa",
    "Espesor PCB",
    "Alto total arriba (comp + PCB)",
    "Alto total abajo (pines + PCB)",
};

// --- Dev board (ESP/Arduino) con USB y 4 agujeros simetricos ---
static const char* const TPL_DEVBOARD[] = {
    "Largo placa",
    "Ancho placa",
    "Espesor PCB",
    "Alto total arriba (comp + PCB)",
    "Alto total abajo (pines + PCB)",
    "USB: ancho abertura",
    "USB: alto abertura",
    "USB: borde placa a borde USB",
    "USB: cara inf PCB a base USB",
    "Agujeros: diametro",
    "Agujeros: span exterior X",
    "Agujeros: span exterior Y",
    "Agujero: borde placa a borde X",
    "Agujero: borde placa a borde Y",
};

// --- Placa + display (OLED/LCD): suma la ventana visible ---
static const char* const TPL_DISPLAY[] = {
    "Largo placa",
    "Ancho placa",
    "Espesor PCB",
    "Alto total abajo (pines + PCB)",
    "USB: ancho abertura",
    "USB: alto abertura",
    "USB: borde placa a borde USB",
    "USB: cara inf PCB a base USB",
    "Display: alto total (mod + PCB)",
    "Display: ventana visible largo",
    "Display: ventana visible ancho",
    "Display: borde sup a ventana",
    "Display: borde izq a ventana",
    "Agujeros: diametro",
    "Agujeros: span exterior X",
    "Agujeros: span exterior Y",
    "Agujero: borde placa a borde X",
    "Agujero: borde placa a borde Y",
};

// --- Modulo / sensor breakout (chico, con header de pines) ---
static const char* const TPL_SENSOR[] = {
    "Largo placa",
    "Ancho placa",
    "Espesor PCB",
    "Alto total arriba (comp + PCB)",
    "Alto total abajo (pines + PCB)",
    "Header: largo fila de pines",
    "Header: borde placa a header",
    "Agujeros: diametro",
    "Agujero: borde placa a borde X",
    "Agujero: borde placa a borde Y",
};

// --- Panel con botones y LEDs (cara frontal) ---
static const char* const TPL_PANEL[] = {
    "Largo placa",
    "Ancho placa",
    "Espesor PCB",
    "Alto total arriba (comp + PCB)",
    "Boton 1: borde placa a borde X",
    "Boton 1: borde placa a borde Y",
    "Boton 1: diametro cap",
    "Boton 1: alto total (cap + PCB)",
    "Boton 2: borde placa a borde X",
    "Boton 2: borde placa a borde Y",
    "LED: borde placa a LED X",
    "LED: borde placa a LED Y",
    "Agujeros: diametro",
    "Agujero: borde placa a borde X",
    "Agujero: borde placa a borde Y",
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
