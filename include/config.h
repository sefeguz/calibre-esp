// Calibre-ESP — configuración de hardware y constantes
// Tarjetas objetivo: ESP32-C3 (SuperMini / DevKitM-1) y XIAO ESP32-C6 (batería)
#pragma once

#define FIRMWARE_VERSION "1.8.1"
#define DEVICE_NAME_DEFAULT "Calibre-ESP"

// ---------------------------------------------------------------------------
// Pines — selección por placa.
// CLK/DATA del calibre van en GPIO0/GPIO1: son canales de ADC1, necesarios
// para el modo fallback ADC (señales de 1.5 V). No usar pull-ups internos: el
// calibre corre de su propia pila y solo comparte GND.
// ---------------------------------------------------------------------------
#if defined(ARDUINO_XIAO_ESP32C6)
  // --- XIAO ESP32-C6 (versión a batería) ---
  // En la XIAO C6 solo D0/D1/D2 (GPIO0/1/2) son ADC1. Sin botón físico (la
  // captura es por web/MCP), GPIO2 queda libre para el divisor de batería.
  // LED en GPIO15 (activo bajo). EVITAR GPIO3/GPIO14 (control de antena RF).
  #define BOARD_XIAO_C6    1
  #define PIN_CALIPER_CLK  0   // D0 / ADC1_CH0
  #define PIN_CALIPER_DATA 1   // D1 / ADC1_CH1
  #define PIN_BUTTON       9   // botón BOOT onboard (captura; futuro wake del
                               // light sleep — GPIO9 despierta del light sleep)
  #define PIN_LED          15  // LED onboard (activo bajo)
  #define LED_INVERTED     true
  #define BATT_ADC_PIN     -1  // sin medición de batería por ahora (GPIO2 libre)
  // Antena: GPIO14=LOW (interna) y GPIO3=LOW (enable del switch RF)
  #define PIN_RF_SWITCH_EN 3
  #define PIN_ANT_SELECT   14
  // Versión a batería: WiFi APAGADO por defecto (ahorro). Se enciende
  // manteniendo BOOT 2 s; el LED queda fijo cuando el WiFi está prendido.
  #define WIFI_OFF_BY_DEFAULT 1
  // Auto-apagado del WiFi tras inactividad (nadie en la web/hotspot ni
  // requests). Mantenerlo prendido = tener la página abierta.
  #define WIFI_IDLE_MS  120000   // 2 min sin uso -> WiFi off (vuelve a BLE)
#else
  // --- ESP32-C3 SuperMini / DevKitM-1 (versión de banco, USB) ---
  #define PIN_CALIPER_DATA 1   // ADC1_CH1 (cableado real de la unidad SuperMini)
  #define PIN_CALIPER_CLK  0   // ADC1_CH0
  #define PIN_BUTTON       9   // botón BOOT onboard (strapping, solo al reset)
  #define PIN_LED          8   // LED onboard (lógica invertida)
  #define LED_INVERTED     true
  #define BATT_ADC_PIN     -1  // sin medición de batería (corre por USB)
#endif

// ---------------------------------------------------------------------------
// Batería (LiPo en placas con divisor a un pin ADC, p.ej. XIAO C6). La C6 NO
// trae divisor de fábrica: se agregan 2x200k de BAT+ a BATT_ADC_PIN y se lee
// con analogReadMilliVolts × BATT_DIVIDER. Hasta soldar el divisor+batería,
// el % muestra valores sin sentido (pin flotante) — es esperado.
// ---------------------------------------------------------------------------
#define BATT_DIVIDER      2.0f  // ratio del divisor (2x200k = 1:2)
#define BATT_FULL_MV      4200  // 100%
#define BATT_EMPTY_MV     3300  // 0% (corte seguro)

// ---------------------------------------------------------------------------
// Protocolo del calibre (24 bits, LSB primero)
// [0..19] valor | [20] signo | [21..22] s/u | [23] bandera pulgadas
// mm: cuentas/100 (0.01 mm)  |  inch: cuentas/2000 (0.0005")
// ---------------------------------------------------------------------------
#define CALIPER_PACKET_BITS    24
#define CALIPER_BIT_GAP_US     1000     // gap > 1 ms entre bits => reinicia paquete
#define CALIPER_IDLE_OFF_MS    250      // sin clock > 250 ms => calibre apagado
#define CALIPER_ADC_THRESH_MV  800      // umbral modo ADC (señales 1.5 V)
#define CALIPER_ADC_THRESH_RAW 1000     // ídem en cuentas crudas (12-bit/11db,
                                        // ~610 mV) — el camino caliente usa
                                        // analogRead sin calibrar por velocidad
#define CALIPER_DETECT_MS      700      // ventana de auto-detección (mayor al
                                        // gap entre paquetes, aún a 3 Hz)

// Prioridad de la tarea de muestreo ADC. En el C3 single-core tiene que estar
// POR ENCIMA de async_tcp (10) y cerca de WiFi (23): a prioridad baja,
// cualquier tarea de red que interrumpa >150 µs durante la ráfaga de ~7 ms
// rompe el paquete (con prio 2 se perdían el 100% de los frames). El sueño
// adaptativo entre paquetes evita inanición del resto del sistema.
#define CALIPER_ADC_TASK_PRIO  19

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
#define MDNS_NAME    "calibre"          // fallback si el nombre configurado
                                        // queda vacío; el mDNS real se deriva
                                        // del "Nombre del dispositivo"
#define WIFI_CONNECT_TIMEOUT_MS 30000

// ---------------------------------------------------------------------------
// Botón
// ---------------------------------------------------------------------------
#define BUTTON_DEBOUNCE_MS   30
#define BUTTON_LONGPRESS_MS  2000   // mantener 2 s: WiFi on/off (C6) o zero (C3)
