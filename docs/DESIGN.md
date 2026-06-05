# Calibre-ESP — Diseño

Firmware para **ESP32-C3** que lee el puerto de datos (SPC) de un calibre digital
Hamilton (pila CR2032, 3 V) y retransmite las mediciones por **WiFi (webserver +
WebSocket)** y como **teclado Bluetooth LE (HID)**, con un botón físico para
"escribir" la medición + Enter en cualquier PC/celular emparejado.

Fecha: 2026-06-04

## Contexto

- El calibre tiene el conector clásico de 4 contactos bajo la tapa corrediza:
  `GND, DATA, CLK, VCC` (visto con la tapa hacia arriba, ver fotos `Calibre01/02.png`).
- **Voltaje de señal (verificado con fuentes)**: la pila CR2032 (3 V) **no
  garantiza** señales de 3 V — varios calibres de 3 V (p.ej. Shahe 5403) sacan
  ~1.5 Vpp por compatibilidad con colectores de datos antiguos. El umbral
  digital del ESP32-C3 es VIH ≈ 0.75·3.3 V = **2.475 V**:
  - señal ~3 V → lectura digital directa (sin electrónica) ✔
  - señal ~1.5 V → no llega al VIH; el firmware cae al modo ADC (sin
    electrónica, solo para clock lento) o se usa level-shifter NPN (2
    transistores, señal invertida — soportado por configuración).
  - **Primer paso de bring-up: medir CLK en reposo con téster** (reposa alto).
- Referencias (ambas MIT, clonadas en `reference/`):
  - **MGX3D/EspDRO**: lee calibres de 1.5 V usando el ADC con umbral por software
    (de ahí "sin componentes extra"). Aporta: fallback ADC, layout de paquete,
    webserver/WebSocket, modo debug de señal.
  - **jkent/esp32-caliper**: componente ESP-IDF con ISR por flanco de clock,
    framing por gap (>1 ms resetea el contador de bits) y detección de apagado
    (>200 ms sin clock). Es el mejor algoritmo de lectura; se porta a Arduino.

## Protocolo (BIN6 — calibre chino estándar, 24 bits)

- Paquetes de **24 bits, LSB primero**, ráfagas a ~3–10 Hz.
- CLK **reposa en ALTO**; el dato se muestrea cuando CLK vuelve a ALTO
  (**flanco ascendente**). Nota: algunos tutoriales web dicen "flanco
  descendente", pero las DOS implementaciones de referencia probadas en
  hardware (EspDRO `getBit()` y jkent `if (clock) {...}` con ANYEDGE)
  muestrean en el ascendente — se sigue el código que funciona. Clock en
  ráfaga: ~80–90 kHz en la mayoría, algunos modelos lentos < 4 kHz. Frame
  completo en ~9 ms, gap entre frames ≥ 50–100 ms.
- Layout (0-indexado): bits `[0..19]` = valor absoluto, bit `20` = signo,
  bits `21..22` sin uso, bit `23` = bandera pulgadas.
- Escalado: **mm → cuentas/100** (0.01 mm); **inch → cuentas/2000** (0.0005").
- Framing: gap entre flancos > 1 ms ⇒ cierra el frame con su conteo de bits;
  sin paquetes > 250 ms ⇒ calibre apagado/desconectado.
- **Variantes** detectables por conteo de bits por frame: 21 = iGaging
  (complemento a dos), 48 = Sylvac 2×24 (20480 cuentas/inch). No se decodifican
  (imposible validarlas sin hardware); se exponen en diagnóstico
  (`lastBits`/`lastRaw`) para soportarlas si aparecen.
- El layout puede variar por modelo (hay reportes con la bandera inch en el
  bit 0): validar empíricamente con 0.00 mm, valor conocido, negativo y toggle
  mm/inch.

## Hardware

```
Calibre (puerto SPC)          ESP32-C3
---------------------         -----------------
GND  ──────────────────────── GND
DATA ──────────────────────── GPIO0  (ADC1_CH0)
CLK  ──────────────────────── GPIO1  (ADC1_CH1)
VCC  ── (sin conectar; el calibre usa su pila)

Botón ── BOOT/GPIO9 onboard (INPUT_PULLUP; strapping solo se muestrea al reset)
LED   ── GPIO8 (onboard en C3 SuperMini, lógica invertida)
```

- DATA/CLK en GPIO0/1 a propósito: son canales de **ADC1**, lo que permite el
  modo fallback ADC si la señal resultara ser de 1.5 V (algunos calibres "3 V"
  regulan internamente a 1.5 V).
- Entradas en modo flotante (sin pull-up interno) para no inyectar corriente al
  calibre. No alimentar el calibre desde 3.3 V: se mantiene su pila.
- Botón en GPIO3 (no usar GPIO9/BOOT para evitar interferir el strapping).

## Lectura: dos modos con auto-detección

1. **Modo digital (ISR)** — si la señal llega a ~3 V:
   ISR en **ambos flancos** de CLK (ANYEDGE, como jkent); el bit de DATA se
   acumula solo cuando CLK queda ALTO (= flanco ascendente, donde el dato es
   estable). Con `invert` (NPN) se niegan CLK y DATA en software. Framing por
   gap con `esp_timer_get_time()` (>1 ms cierra el frame con su conteo de
   bits), encola a una cola FreeRTOS. El frame "colgado" tras el último bit lo
   finaliza `poll()` apenas vence el gap. ISR registrado vía IDF con
   `ESP_INTR_FLAG_IRAM` (sobrevive escrituras de flash NVS/OTA). Mínimo costo
   de CPU.
2. **Modo ADC (polling)** — fallback para señales de ~1.5 V (estilo EspDRO):
   tarea que muestrea `analogReadMilliVolts()` con umbral 800 mV. Solo sirve
   para calibres de clock lento (<~4 kHz). En el C3 single-core duerme ~60% del
   intervalo entre paquetes (medido adaptativamente) para no matar al idle task
   (watchdog).
3. **Auto-detección** al arrancar (y reintentos cada 1 s):
   - Contar flancos digitales (CHANGE) en CLK por 700 ms → si ≥ ~40, digital.
   - Si no, muestrear ADC: actividad con max > 800 mV y min < 400 mV → ADC.
   - Si nada, "sin calibre"; se informa por web/serial y se reintenta.
4. Validación: solo frames de exactamente 24 bits; lecturas > ±400 mm se
   descartan. Frames de 21/48 bits (variantes iGaging/Sylvac) se cuentan y
   exponen en `diag()` sin decodificar.

## Software (PlatformIO, framework Arduino)

```
src/
  main.cpp          — orquestación: init módulos, loop (botón, LED, broadcast)
  caliper.{h,cpp}   — lector dual-modo + decodificador + estado (on/off/modo)
  ble_keyboard.{h,cpp} — teclado HID BLE (NimBLE), envía la medición como texto
  webserver.{h,cpp} — AsyncWebServer: UI, WebSocket /ws, REST, OTA, portal AP
  settings.{h,cpp}  — Preferences (NVS): WiFi, separador decimal, tecla EOL, etc.
  button.{h,cpp}    — antirrebote; corto = capturar/enviar, largo = zero relativo
  web_ui.h          — single-page UI embebida en PROGMEM (sin filesystem)
include/config.h    — pines, constantes, versión
```

### WiFi
- Modo STA con credenciales en NVS; si falla (30 s) → AP `Calibre-ESP`
  (pass `calibre123`) con **portal cautivo** para configurar.
- mDNS: `http://calibre.local`.
- Coexistencia BLE+WiFi: power-save de WiFi activo (default), payloads chicos.

### Web UI (embebida, sin apps)
- Display grande de la medición en vivo (WebSocket, ~10 Hz).
- Hold / Zero relativo / mm-inch / min-max-promedio.
- Lista de capturas (botón físico o botón web) con export **CSV**.
- Página de configuración: WiFi, separador decimal (`,` o `.`), tecla final
  (Enter/Tab/ninguna), nombre del dispositivo, modo de lectura (auto/digital/ADC).
- `/update`: OTA de firmware desde el navegador.

### REST API
- `GET /api/value` → JSON última lectura `{value_mm, raw, unit, on, mode}`
- `GET /api/history` → buffer circular de lecturas
- `POST /api/capture` → captura (igual que botón)
- `GET/POST /api/config` → configuración

### Teclado BLE (HID over GATT)
- ESP32-C3 = solo BLE (sin BT clásico) → HID BLE, compatible con
  Windows/Android/iOS/macOS/Linux.
- Implementación propia compacta sobre **NimBLE-Arduino** (`NimBLEHIDDevice`),
  sin dependencia de la lib abandonada T-vK.
- **Keycodes independientes del layout**: dígitos de la fila superior (0x1E..0x27),
  menos del keypad (0x56), separador decimal `,`(0x36)/`.`(0x37) — idénticos en
  layouts US/ES/LatAm — y Enter/Tab configurables. Default `,` (Excel es-AR).
- Emparejado Just-Works con bonding (requerido por HID en Windows).

### Botón físico (GPIO3)
- Corto: captura la medición → la tipea por BLE (si hay host conectado) +
  la agrega al log web.
- Largo (>1.5 s): zero relativo (toggle).
- LED: doble parpadeo corto al capturar; patrones para AP/STA/BLE conectado.

## Particiones y memoria
- Partición `min_spiffs` (app 1.9 MB ×2) para que WiFi+BLE+AsyncWebServer quepan
  y quede soporte OTA.
- UI en PROGMEM (gzip opcional) — no se usa SPIFFS/LittleFS para servir archivos.

## Verificación
1. `pio run` compila sin errores (env `esp32c3-supermini` y `esp32c3-devkitm`).
2. Flasheo + monitor serie: ver auto-detección de modo y stream JSON de lecturas.
3. Mover el calibre → la web en `calibre.local` actualiza en vivo.
4. Emparejar BLE desde una PC, abrir un editor, presionar el botón → aparece la
   medición + Enter.
5. Probar export CSV y OTA.
