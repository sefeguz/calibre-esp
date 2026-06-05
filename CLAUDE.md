# Calibre-ESP

Firmware ESP32-C3 (PlatformIO/Arduino) que lee un calibre digital Hamilton por
su puerto SPC y lo expone por web (UI + WebSocket + REST), teclado BLE HID y
serial. Incluye un servidor MCP para que Claude lea mediciones directamente.

**Estado: funcionando en hardware real** (~11.7 Hz, 99.2% frames OK).
Repo: https://github.com/sefeguz/calibre-esp

## Hardware (esta unidad concreta)

- Placa: **ESP32-C3 SuperMini**, USB nativo, aparece como **COM11**
  (VID:PID 303A:1001, MAC 1C:DB:D4:D4:61:48).
- Calibre: Hamilton naranja, pila CR2032 (3 V) pero **señales de 1.5 V**
  (regula interno — medido con téster; muy común). Clock lento ~3.2 kHz
  (fases 102–205 µs), paquetes cada ~112 ms.
- **Cableado real de esta unidad: CLK=GPIO0, DATA=GPIO1** (quedaron así
  soldados; si se recablea, ajustar `PIN_CALIPER_*` en `include/config.h`).
  GND común, VCC del calibre SIN conectar. Botón = BOOT onboard (GPIO9).
  LED = GPIO8 (invertido).
- En la red del usuario: **http://192.168.1.43** (`calibre.local` NO resuelve
  desde su PC Windows; sí suele andar desde celulares). AP fallback:
  `Calibre-ESP` / `calibre123`.

## Protocolo (verificado en esta unidad)

BIN6 24 bits LSB-first: bits[0..19] valor, bit20 signo, bit23 inch.
mm = cuentas/100; inch = cuentas/2000. CLK reposa ALTO; **el dato se muestrea
cuando CLK vuelve a ALTO (flanco ascendente)** — ojo: varios tutoriales web
dicen flanco descendente, pero las dos referencias probadas en hardware
(EspDRO y jkent/esp32-caliper, en `reference/`, gitignored) y esta unidad
muestrean en el ascendente. Frames de 21/48 bits = variantes iGaging/Sylvac,
solo se cuentan en diagnóstico.

## Arquitectura del firmware

```
src/main.cpp        orquestación; ÚNICO lugar donde corren las acciones que
                    mutan estado (capturas, zero, clear) — ver Concurrencia
src/caliper.{h,cpp} lector dual-modo + decode + filtros anti-glitch
src/ble_keyboard.*  teclado HID BLE propio sobre NimBLE 2.x (T-vK está roto en C3)
src/webserver.*     ESPAsyncWebServer: UI PROGMEM, /ws, /api/*, OTA /update,
                    portal cautivo en modo AP
src/settings.*      Preferences/NVS (WiFi, separador decimal, EOL, modo, invert)
src/app.{h,cpp}     estado compartido: capturas (ring + mux), zero relativo,
                    pedidos diferidos appRequest*/appConsume*
src/button.*        antirrebote: corto=capturar, largo(1.5s)=zero relativo
src/web_ui.h        single-page UI embebida (es-AR, export CSV formato Excel ES)
```

### Lector del calibre (lo más delicado)

Dos modos con auto-detección al boot (reintenta cada 1 s si no hay señal):

1. **DIGITAL (ISR)** — solo si la señal llega a ~3 V (VIH C3 ≈ 2.47 V).
   ANYEDGE en CLK, acumula bit cuando CLK queda alto. Registrado vía IDF
   con `ESP_INTR_FLAG_IRAM` (sobrevive escrituras de flash NVS/OTA).
2. **ADC por DMA (`adc_continuous`)** — el modo que usa ESTA unidad (1.5 V).
   80 kS/s totales (40 k por canal), decodifica la onda con histéresis
   (umbral subida 1400 / bajada 800 cuentas crudas). **Inmune a la
   preempción de WiFi/BLE** — esa fue la clave: el polling con analogRead
   (estilo EspDRO) perdía 50–100% de los frames con la radio activa porque
   en el C3 single-core cualquier interrupción >100 µs durante la ráfaga
   de ~7 ms rompía el paquete.

Filtros anti-glitch en `acceptReading()`: saltos >2 mm requieren que el frame
siguiente salte en la misma dirección; cambio de unidad requiere dos frames
iguales. Cambios chicos pasan sin latencia (caso de uso: medir fino).

### Concurrencia (C3 single-core — REGLAS IMPORTANTES)

- Los handlers de ESPAsyncWebServer corren en la tarea `async_tcp` (prio 10),
  NO en loop() (prio 1). **Nunca** mutar estado ni tipear BLE desde un
  handler: usar `appRequest*()` y dejar que loop() lo consuma
  (`appConsume*()`), o flags tipo `Caliper::redetect()`.
- `Caliper::poll/begin/end` solo desde loop(); `redetect()` y `diag()` son
  thread-safe.
- Mientras el modo ADC-DMA está activo, el driver continuo es dueño del ADC:
  cualquier `analogRead` falla. `diag()` usa los niveles cacheados del
  stream; los comandos `pins`/`scope` hacen `caliper.end()` antes.
- uint64 en RV32 no es atómico: leer/escribir `_lastFrameRaw` y `_last`
  bajo `_mux`.
- Tareas que muestrean deben ceder CPU (watchdog del idle task) — la tarea
  DMA bloquea en `adc_continuous_read`, OK.
- Apagar la tarea DMA por flag (`_dmaStop`) y recién después
  `adc_continuous_stop/deinit` — nunca `vTaskDelete` a mitad de una lectura.

### BLE (NimBLE 2.x)

- Pairing: bonding + **Just Works legacy** (sc=false). Con Secure
  Connections y NoInputNoOutput, Windows fallaba.
- **NO** llamar `updateConnParams` en `onConnect` (corta el pairing en
  Windows).
- `isConnected()` = conectado **y suscripto al CCCD** del input report
  (notify antes de la suscripción se descarta en silencio → falsos éxitos).
- Keycodes layout-safe (US/ES/LatAm): dígitos fila superior, keypad-minus
  0x56, coma 0x36, punto 0x37. Separador decimal configurable (default `,`
  para Excel es-AR).
- Bonds rotos bloquean reconexión: comando serial `blereset` + "Quitar
  dispositivo" en el host.

### WebSocket

Con WiFi en power-save (obligatorio por coexistencia BLE) la radio drena
lento: **no encolar de más**. Solo cambios (≥0.005 mm, máx ~5/s) + heartbeat
1 s, y saltear si `!ws.availableForWriteAll()` (mejor perder un frame viejo
que acumular 4 s de atraso, que es lo que pasaba).

## Build / flash / debug

```powershell
# pio NO está en PATH:
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32c3-supermini
# flashear (¡SIEMPRE con PYTHONUTF8! esptool 5.x crashea en consola cp1252):
$env:PYTHONUTF8 = "1"
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32c3-supermini -t upload --upload-port COM11
```

- Plataforma: pioarduino 55.03.39 pineado por URL (¡el tag NO lleva `v`!).
  Requiere PlatformIO Core ≥6.1.19.
- Envs: `esp32c3-supermini` (USB CDC, el que se usa) y `esp32c3-devkitm`.
- Particiones `min_spiffs` (OTA disponible en `/update`).
- Serial 115200. Al abrir el puerto con pyserial usar `rts=False, dtr=False`
  ANTES de `open()` (si no, resetea/bootloadea la placa). Tras un reset el
  CDC se re-enumera y se pierde el log de boot.
- Comandos serial: `status` `start` `stop` `redetect` `capture` `zero`
  `pins` (test de cableado con pull-up) `scope` (mini analizador lógico ADC)
  `blereset`.
- Diagnóstico remoto: `GET /api/status` (frames ok/bad, mV, heap, RSSI).

## MCP (Claude lee el calibre)

`mcp/calibre_mcp.py` (correr con `uv run --script`; deps inline) registrado
en `.mcp.json`. Herramienta clave: `esperar_captura(etiqueta)` — espera a que
el usuario apriete BOOT y devuelve el valor (polling de `/api/captures`; si
da timeout, re-llamar — no es error). Caso de uso: sesiones de medición
guiadas para diseñar piezas 3D. `CALIBRE_URL` en `.mcp.json` si cambia la IP.

## Pendiente de validar

- Valor negativo (bit 20) y pulgadas reales (bit 23) con el calibre físico.
- Tipeo BLE end-to-end con el botón BOOT (post-fix de pairing).
- Variantes 21/48 bits: si `lastBits` ≠ 24 en diag, guardar `lastRaw` y
  analizar antes de intentar decodificar.
