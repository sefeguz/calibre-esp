# Calibre-ESP

Firmware para **ESP32-C3** que lee el puerto de datos (SPC) de un calibre
digital chino (probado con un Hamilton a pila CR2032) y retransmite las
mediciones por:

- **WiFi**: interfaz web con display en vivo, capturas, export CSV y configuración
  (`http://calibre.local`)
- **Bluetooth LE**: actúa como **teclado HID** — al presionar el botón físico
  "tipea" la medición + Enter en la PC/celular emparejado (ideal para cargar
  planillas)
- **USB serial**: stream JSON + comandos de diagnóstico

Basado en [MGX3D/EspDRO](https://github.com/MGX3D/EspDRO) (modo ADC) y
[jkent/esp32-caliper](https://github.com/jkent/esp32-caliper) (decodificador
por interrupciones), ambos MIT.

## Cableado

El calibre tiene 4 contactos bajo la tapa corrediza (visto desde el frente,
de izquierda a derecha): **GND, DATA, CLK, VCC**.

```
Calibre (puerto SPC)          ESP32-C3
---------------------         -----------------
GND  ──────────────────────── GND
CLK  ──────────────────────── GPIO0
DATA ──────────────────────── GPIO1
VCC  ── (NO conectar: el calibre usa su propia pila)

Botón ── el botón BOOT (GPIO9) que ya trae la placa C3 SuperMini
LED   ── GPIO8 (ya viene en la placa C3 SuperMini)
```

> Probado en hardware real (Hamilton CR2032): señal de 1.5 V → modo ADC
> automático, clock ~3.2 kHz, ~96% de frames OK con WiFi+BLE activos.
> Si DATA/CLK quedan al revés, ajustar `PIN_CALIPER_*` en `include/config.h`.

> **Importante — verificá el nivel de señal primero.** Que el calibre use pila
> de 3 V **no garantiza** que las señales DATA/CLK lleguen a 3 V: muchos
> calibres con CR2032 sacan solo ~1.5 V por compatibilidad. Con un téster en
> el pad CLK (que reposa en nivel alto) contra GND:
> - **CLK ≈ 2.6–3 V** → lectura digital directa, sin componentes extra. ✔
> - **CLK ≈ 1.5 V** → el firmware cae automáticamente al **modo ADC** (también
>   sin componentes extra), que funciona con calibres de clock lento. Si tu
>   calibre transmite en ráfagas rápidas (~90 kHz) y el modo ADC no engancha,
>   la solución robusta son 2 transistores NPN (ver abajo).
>
> El firmware **auto-detecta** el modo al arrancar y lo muestra en la web y por
> serial (`status`). No conectes nada a VCC ni uses pull-ups: las entradas
> quedan flotantes y el calibre sigue funcionando con su pila (solo GND común).

### Level-shifter NPN (solo si hace falta)

Para calibres de 1.5 V con clock rápido: un transistor NPN (BC548/2N2222) por
línea — señal del calibre → resistencia 10k → base; colector → GPIO con
pull-up 10k a 3.3V; emisor → GND. Esto **invierte** la señal: activá
"Señal invertida" en la configuración web.

## Compilar y flashear (PlatformIO)

```sh
# placa C3 SuperMini (USB nativo)
pio run -e esp32c3-supermini -t upload

# placa ESP32-C3-DevKitM-1 (puente USB-UART)
pio run -e esp32c3-devkitm -t upload

# monitor serie
pio device monitor
```

## Primer uso

1. Flasheá y abrí el monitor serie (115200): vas a ver la auto-detección del
   modo de lectura y el stream de mediciones.
2. El equipo levanta el AP WiFi **`Calibre-ESP`** (clave `calibre123`). Conectate
   y el portal cautivo te lleva a la configuración: cargá tu red WiFi y guardá.
3. Tras reiniciar queda en tu red: **`http://calibre.local`** (o la IP que
   muestra el serial).
4. Para el teclado BLE: en la PC/celular buscá dispositivos Bluetooth y
   emparejá **`Calibre-ESP`**. Abrí cualquier campo de texto/Excel y presioná
   el botón físico: aparece la medición + Enter.

## Uso

| Acción | Efecto |
|---|---|
| Botón corto | Captura la medición: la tipea por BLE y la agrega al log web |
| Botón largo (1.5 s) | Zero relativo on/off (medir diferencias) |
| Web: Capturar / Zero / Hold / mm⇄in | Lo mismo desde el navegador |
| Web: Exportar CSV | Descarga las capturas (formato Excel ES: `;` y coma decimal) |
| `http://calibre.local/update` | Actualización de firmware OTA |

### Configuración (web)

- **Separador decimal** del teclado BLE: `,` (default, Excel en español) o `.`
- **Tecla final**: Enter / Tab / ninguna
- **Modo de lectura**: auto / digital (3 V) / ADC (1.5 V)
- **Señal invertida**: para el level-shifter NPN

### API REST

| Endpoint | Descripción |
|---|---|
| `GET /api/value` | Última lectura `{mm, counts, unit, on, rel, ts}` |
| `GET /api/status` | Diagnóstico completo (modo, frames, niveles mV, heap, RSSI) |
| `POST /api/capture` | Captura (igual que el botón) |
| `POST /api/zero` | Zero relativo on/off |
| `GET /api/captures` / `DELETE` | Log de capturas / borrar |
| `GET /api/captures.csv` | Export CSV |
| `GET/POST /api/config` | Configuración |
| `POST /api/redetect` | Re-detectar señal del calibre |

WebSocket en `/ws`: lecturas en vivo `{"t":"r","v":12.34,...}` (~8 Hz).

### Comandos serial

`start` / `stop` (stream JSON) · `status` (diagnóstico) · `redetect` ·
`capture` · `zero`

## Diagnóstico sin osciloscopio

`GET /api/status` (o el comando serial `status`) muestra:

- `clkMv` / `dataMv`: nivel instantáneo de las líneas — CLK en reposo debería
  marcar el nivel alto de la señal (~3000 mV o ~1500 mV).
- `lastBits`: bits del último frame. **24** = protocolo estándar (BIN6).
  21 o 48 = variante del protocolo (iGaging/Sylvac) — reportala como issue con
  el `lastRaw` para agregar soporte.
- `framesOk` / `framesBad`: salud de la lectura.

## Protocolo (referencia)

Frames de 24 bits LSB-primero; CLK reposa alto y el dato se lee cuando CLK
vuelve a alto (flanco ascendente, igual que EspDRO y jkent). Ráfagas a
~3–10 Hz, gap entre frames > 50 ms:

```
bits [0..19] = valor (binario)     mm:   valor / 100   (0.01 mm)
bit  [20]    = signo (1 = neg.)    inch: valor / 2000  (0.0005")
bit  [23]    = 1 si está en pulgadas
```

## Licencia

MIT. Incluye código derivado de EspDRO © 2018 Marius G y esp32-caliper
© 2025 Jeff Kent, ambos MIT.
