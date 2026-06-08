# Calibre-ESP

Firmware ESP32-C3 (PlatformIO/Arduino) que lee un calibre digital Hamilton por
su puerto SPC y lo expone por web (UI + WebSocket + REST), teclado BLE HID y
serial. Incluye un servidor MCP para que Claude lea mediciones directamente.

**Estado: v1.2.0 desplegado y verificado en hardware real** (2026-06-07):
~11.8 Hz, 99.3% frames OK, latencia medición→web ~150-300 ms. UI con sidebar
(En vivo / Medición / Capturas / Config), modo claro/oscuro y mobile
(bottom-nav <760px). Repo: https://github.com/sefeguz/calibre-esp
El equipo está montado en un case impreso en 3D.

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
- En la red del usuario: **http://192.168.1.43** o **http://calibre-esp.local**
  (el mDNS se deriva del "Nombre del dispositivo" de Config, sanitizado;
  verificado resolviendo desde su PC). AP `Calibre-ESP`/`calibre123`
  disponible ~2 s tras encender, se apaga solo al conectar la STA.

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
                    mutan estado (capturas, zero, clear) — ver Concurrencia.
                    captureAction(): con sesión activa el valor va al ítem
                    actual de la sesión; si no, al log + tipeo BLE
src/session.*       sesión de medición guiada (lista nombrada que se completa
                    con el botón; select para repetir; confirm → MCP la
                    retira). Estado en RAM con mux propio; máx 24 ítems
src/templates.h     plantillas de medición para cajitas IoT (5: simple,
                    devboard, display, sensor, panel). Datos const en flash;
                    GET /api/templates lista, POST /api/session/template
                    {id} inicia. Filosofía "medir bordes, derivar centros":
                    los ítems piden "borde a centro", "separacion (centros)",
                    stack Z (alto sobre/bajo PCB) — ver help_text.h/llms.txt
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

Filtro anti-glitch en `acceptReading()`: **mediana de 3** — salida a tasa
completa (un frame de salida por frame de entrada, ~11.7 Hz), lag fijo de
1 frame (~112 ms), y un frame corrupto aislado (valor o bit de unidad falso)
nunca es la mediana. NO usar filtros por confirmación de salto: bajan la tasa
a la mitad durante el movimiento y el usuario lo nota (feedback explícito:
quiere refresco instantáneo, <1 s siempre).

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
lento: **no encolar de más**. Cambios cada ≥90 ms (~11 Hz) con flush
garantizado del último valor (un cambio suprimido se envía apenas vence el
intervalo) + heartbeat 500 ms, y saltear si `!ws.availableForWriteAll()`
(mejor perder un frame viejo que acumular 4 s de atraso, que es lo que
pasaba al encolar todo).

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
  `blereset` `sim <mm>` (inyecta lectura simulada ~10 s — clave para probar
  capturas/sesiones/UI SIN el calibre conectado; el usuario lo desconecta
  para no gastarle la pila).
- ⚠️ `AsyncCallbackJsonWebHandler` matchea por PREFIJO de URL: registrar las
  rutas específicas (`/api/session/select`) ANTES que la genérica
  (`/api/session`), si no la genérica se las traga y devuelve 400.
- ⚠️ Contenido grande (la UI, ~28 KB) SIEMPRE con
  `send(200, tipo, (const uint8_t*)PTR, strlen(PTR))` — la sobrecarga con
  `const char*`/String COPIA todo al heap por request; con varios clientes
  el heap tocó 2 KB (minHeap) y corrompía respuestas (scrambling).
- WiFi multi-red (hasta 10 en NVS, `settings.wifi[]`), máquina de estados
  `wifiTick()` en webserverLoop. **Reglas críticas (1 sola radio en el C3):**
  - El **SoftAP queda SIEMPRE prendido** (AP_STA); nunca `softAPdisconnect`.
    Al conectar la STA solo se baja el portal cautivo (`captivePortalOff`).
    Así el hotspot no "desaparece" y el equipo es alcanzable por AP+LAN.
  - El roaming SÍ corre aunque haya cliente en el AP (el AP nunca se apaga,
    solo hay un parpadeo de canal al conectar). NO bloquear por `apBusy`:
    un celular pegado al hotspot impediría que el equipo se conecte a la red
    recién configurada (bug v1.5.1, corregido en v1.5.2). El bug original de
    "el hotspot se desconecta a los 30s" lo arregla el AP-siempre-prendido.
  - Guardar config con redes nuevas → reconexión inmediata: el handler
    setea `wifiReapplyPending` y `wifiTick` suelta la STA y re-evalúa DESPUÉS
    de ~1.2 s (NO en el handler: `WiFi.disconnect()` ahí corta la respuesta
    HTTP en curso y el cliente recibe un error).
  - **Escaneo de UI** (`/api/wifi/scan` → `uiScanPending`) separado del de
    roaming: solo cachea, NUNCA conecta. Un scan estando conectado tira la
    STA ~2 s; se recupera con `WiFi.reconnect()` inmediato + nudge cada 5 s
    en estado CONNECTED (recupera en 4-6 s, antes quedaba caída para siempre).
  - Caché de scan 15 s. POST /api/config recibe `wifi: [{ssid, pass}]`
    (pass vacía = conservar la guardada). Migración del formato viejo de 1 red.
  - ⚠️ UI: "Guardar" debe llamar `addWifi(true)` primero para recoger un SSID
    tipeado a mano que no se "Agregó" (si no, se pierde — era un bug).
  - **IP estática por red** (v1.6.0): cada `WifiNet` tiene `staticIp` + ip/gw/
    sn/dns; en NVS se empaca como "wx<i>"="ip|gw|sn|dns" (vacío=DHCP).
    `wifiTryCandidate` hace `WiFi.config(...)` o resetea a DHCP con
    `WiFi.config(0,0,0,0)` (hay que resetear explícito al pasar de estática a
    DHCP). UI: selector DHCP/Manual por red, editar (lápiz) y ver password (ojo).
- **WebSocket — anti-acumulación (v1.6.1, importante):** el navegador, si la
  página queda abierta durante un firmware update, reconecta varias veces y
  dejaba conexiones WS DUPLICADAS que saturaban los sockets del equipo (la 6ª
  conexión daba timeout) → lag de "valores de a uno con delay". Fixes:
  (a) JS: guard en `connect()` — no abrir WS nuevo si ya hay uno
  conectando/abierto, un solo timer de reconexión;
  (b) `ws.cleanupClients(4)` limita el pool;
  (c) `wsSendFresh()` envía por-cliente solo si `canSend()` (un cliente lento
  no frena a los demás) — reemplazó `availableForWriteAll()` que era
  todo-o-nada. Tras tocar esto, el usuario debe **recargar la página con
  Ctrl+Shift+R** (su pestaña tiene el JS viejo hasta entonces).
- Heap libre en operación normal ~32-40 KB (v1.6.x; bajó de ~57 KB en v1.5
  por las Strings de IP estática y la UI más grande). minHeap estable ~20 KB,
  sin fuga. Margen OK; a vigilar si se agregan features grandes.
- Diagnóstico serial: comando `status` ahora incluye línea `wifi:` (STA/AP,
  IPs, clientes, heap) vía `webserverWifiInfo()`; comando `reboot`.
- `GET /llms.txt`: guía para asistentes IA (API + flujo de sesiones) — un
  LLM con solo HTTP puede autodescubrir el dispositivo. La vista Ayuda de la
  UI tiene un botón que la copia al portapapeles.
- La UI se prueba con Playwright contra el equipo real (desktop + 375×812);
  `sim` + `POST /api/capture` simulan el botón.
- Diagnóstico remoto: `GET /api/status` (frames ok/bad, mV, heap, RSSI).

## MCP (Claude lee el calibre)

`mcp/calibre_mcp.py` (correr con `uv run --script`; deps inline) registrado
en `.mcp.json`. `CALIBRE_URL` en `.mcp.json` si cambia la IP.

**Flujo principal para medir piezas (diseño 3D)**:
1. `nueva_medicion(["ancho interior", "alto", ...])` — la tabla aparece sola
   en la web del calibre (máx 24 etiquetas de 40 chars).
2. Avisar al usuario que la lista está lista.
3. `esperar_mediciones()` en loop (timeout = seguir llamando, no es error;
   devuelve `progreso`). El usuario completa con el botón, puede repetir
   tocando una fila, y CONFIRMA en la web → devuelve `{etiqueta: mm}` y
   limpia la sesión.
También: `esperar_captura(etiqueta)` para una medición suelta, `leer_medicion`,
`cancelar_medicion`, `listar/borrar_capturas`, `zero_relativo`, `estado`.

## Pendiente de validar

- Valor negativo (bit 20) y pulgadas reales (bit 23) con el calibre físico.
- Tipeo BLE end-to-end con el botón BOOT (post-fix de pairing).
- Variantes 21/48 bits: si `lastBits` ≠ 24 en diag, guardar `lastRaw` y
  analizar antes de intentar decodificar.
