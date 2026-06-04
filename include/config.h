// Calibre-ESP — configuración de hardware y constantes
// Tarjeta objetivo: ESP32-C3 (SuperMini / DevKitM-1)
#pragma once

#define FIRMWARE_VERSION "1.0.0"
#define DEVICE_NAME_DEFAULT "Calibre-ESP"

// ---------------------------------------------------------------------------
// Pines (ESP32-C3)
// DATA/CLK van en GPIO0/GPIO1 a propósito: son canales de ADC1, necesarios
// para el modo fallback ADC (señales de 1.5 V). No usar pull-ups internos:
// el calibre se alimenta de su propia pila CR2032 (3 V) y solo comparte GND.
// ---------------------------------------------------------------------------
#define PIN_CALIPER_DATA 0   // ADC1_CH0
#define PIN_CALIPER_CLK  1   // ADC1_CH1
#define PIN_BUTTON       3   // botón a GND, INPUT_PULLUP
#define PIN_LED          8   // LED onboard (C3 SuperMini: lógica invertida)
#define LED_INVERTED     true

// ---------------------------------------------------------------------------
// Protocolo del calibre (24 bits, LSB primero)
// [0..19] valor | [20] signo | [21..22] s/u | [23] bandera pulgadas
// mm: cuentas/100 (0.01 mm)  |  inch: cuentas/2000 (0.0005")
// ---------------------------------------------------------------------------
#define CALIPER_PACKET_BITS    24
#define CALIPER_BIT_GAP_US     1000     // gap > 1 ms entre bits => reinicia paquete
#define CALIPER_IDLE_OFF_MS    250      // sin clock > 250 ms => calibre apagado
#define CALIPER_ADC_THRESH_MV  800      // umbral modo ADC (señales 1.5 V)
#define CALIPER_DETECT_MS      300      // ventana de auto-detección de modo

// ---------------------------------------------------------------------------
// Buffer de historial (lecturas en vivo) y de capturas
// ---------------------------------------------------------------------------
#define HISTORY_SIZE   512    // lecturas en vivo (~1 min a 8 Hz)
#define CAPTURES_SIZE  200    // mediciones capturadas con el botón

// ---------------------------------------------------------------------------
// WiFi / red
// ---------------------------------------------------------------------------
#define AP_SSID      "Calibre-ESP"
#define AP_PASS      "calibre123"
#define MDNS_NAME    "calibre"          // http://calibre.local
#define WIFI_CONNECT_TIMEOUT_MS 30000

// ---------------------------------------------------------------------------
// Botón
// ---------------------------------------------------------------------------
#define BUTTON_DEBOUNCE_MS   30
#define BUTTON_LONGPRESS_MS  1500
