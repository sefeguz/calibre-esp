// Medición de batería (solo placas con divisor a un pin ADC, p.ej. XIAO C6
// con 2x200k de BAT+ a un pin). Si BATT_ADC_PIN == -1, todo devuelve "sin
// batería" (la versión USB / SuperMini).
#pragma once

#include <Arduino.h>
#include "config.h"

// Devuelve el voltaje de la batería en mV, o 0 si no hay medición configurada.
static inline uint32_t batteryMilliVolts()
{
#if BATT_ADC_PIN >= 0
    uint32_t acc = 0;
    for (int i = 0; i < 16; i++) acc += analogReadMilliVolts(BATT_ADC_PIN);
    return (uint32_t)((acc / 16.0f) * BATT_DIVIDER);
#else
    return 0;
#endif
}

// Porcentaje 0-100 (mapeo lineal aproximado; la curva LiPo no es lineal pero
// alcanza para un indicador). -1 si no hay medición.
static inline int batteryPercent()
{
#if BATT_ADC_PIN >= 0
    int mv = (int)batteryMilliVolts();
    int pct = (mv - BATT_EMPTY_MV) * 100 / (BATT_FULL_MV - BATT_EMPTY_MV);
    return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
#else
    return -1;
#endif
}

static inline bool batteryAvailable()
{
    return BATT_ADC_PIN >= 0;
}
