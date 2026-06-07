// Guía para asistentes IA servida en /llms.txt (estándar llms.txt).
// También se muestra en la vista Ayuda de la web (botón "copiar guía").
#pragma once

#include <pgmspace.h>

static const char LLMS_TXT[] PROGMEM = R"llms(# Calibre-ESP - guia para asistentes IA

Dispositivo: ESP32-C3 que lee un calibre digital y expone mediciones en mm.
Base URL: la misma IP/host donde se sirve este archivo.

## Que podes hacer
- Leer la medicion actual del calibre en vivo.
- Pedirle mediciones al usuario con una SESION DE MEDICION GUIADA: vos creas
  la lista de etiquetas, el usuario completa cada una apretando el boton
  fisico del dispositivo (la web muestra la tabla y avanza sola; puede tocar
  una fila para repetirla) y CONFIRMA al final; recien ahi retiras los valores.

## Con el MCP "calibre" conectado (Claude Code)
Herramientas: nueva_medicion(etiquetas[]), esperar_mediciones(),
cancelar_medicion(), esperar_captura(etiqueta), leer_medicion(),
listar_capturas(), borrar_capturas(), zero_relativo(), estado().

Flujo recomendado para medir una pieza:
1. nueva_medicion(["ancho interior","alto","profundidad"])  (max 24 etiquetas, 40 chars c/u)
2. Avisar al usuario que la lista aparecio en la web del calibre.
3. esperar_mediciones() en loop: el timeout NO es error, volver a llamarla
   (devuelve "progreso" tipo "2/5"). Cuando el usuario confirma devuelve
   {etiqueta: valor_mm} y la sesion se limpia sola.

## Solo con HTTP (sin MCP)
- GET    /api/value           -> {"mm":12.34,"counts":1234,"unit":"mm","on":true,"rel":false,"ts":...}
- GET    /api/status          -> diagnostico: modo lectura, frames ok/bad, heap, rssi, ble
- POST   /api/session         body {"items":["ancho","alto"]} -> crea la sesion guiada
- GET    /api/session         -> {"active","confirmed","current","allDone","items":[{"n","v","d"}]}
- POST   /api/session/select  body {"index":N} -> mover el cursor a un item (repetir)
- POST   /api/session/confirm -> confirmar (requiere todos los items medidos)
- DELETE /api/session         -> cancelar / retirar la sesion
- POST   /api/capture         -> captura (equivale al boton fisico)
- GET    /api/captures        -> log de capturas sueltas [{v,age,ts}]
- DELETE /api/captures        -> borrar el log
- GET    /api/captures.csv    -> export CSV
- POST   /api/zero            -> zero relativo on/off
- POST   /api/redetect        -> re-detectar la senal del calibre
Patron sin MCP: POST /api/session -> poll GET /api/session cada ~1 s hasta
"confirmed":true -> leer items -> DELETE /api/session.

## Reglas importantes
- Valores siempre en mm (float; la resolucion real del calibre es 0.01 mm).
- NO retirar mediciones antes de "confirmed":true (el usuario puede seguir
  corrigiendo filas hasta confirmar).
- "on":false en /api/value = calibre apagado o desconectado: pedirle al
  usuario que lo conecte/encienda antes de medir.
- WebSocket en /ws: {"t":"r"} lecturas en vivo, {"t":"cap"} captura,
  {"t":"ses"} cambio de sesion, {"t":"st"} estado.

## Contexto de hardware
Boton fisico: corto = capturar (o llenar el item actual de la sesion);
largo 1.5 s = zero relativo. El calibre transmite ~9 lecturas/s. El equipo
tambien actua como teclado BLE: tipea la medicion en el host emparejado.
)llms";
