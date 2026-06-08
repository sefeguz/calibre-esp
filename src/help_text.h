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
  la lista de etiquetas (o usas una PLANTILLA lista para cajitas IoT), el
  usuario completa cada una apretando el boton fisico del dispositivo (la web
  muestra la tabla y avanza sola; puede tocar una fila para repetirla) y
  CONFIRMA al final; recien ahi retiras los valores.

## Con el MCP "calibre" conectado (Claude Code)
Herramientas: listar_plantillas(), iniciar_plantilla(id),
nueva_medicion(etiquetas[]), esperar_mediciones(), cancelar_medicion(),
esperar_captura(etiqueta), leer_medicion(), listar_capturas(),
borrar_capturas(), zero_relativo(), estado().

Flujo recomendado para disenar una cajita 3D (lo mas comun):
1. iniciar_plantilla("devboard")  -- o "display"/"sensor"/"panel"/"simple",
   ver listar_plantillas(). O nueva_medicion([...]) para una lista a medida
   (max 24 etiquetas, 40 chars c/u).
2. Avisar al usuario que la lista aparecio en la web del calibre.
3. esperar_mediciones() en loop: el timeout NO es error, volver a llamarla
   (devuelve "progreso" tipo "2/5"). Cuando el usuario confirma devuelve
   {etiqueta: valor_mm} y la sesion se limpia sola.

## Como medir para una cajita (filosofia: medir facil, derivar dificil)
El calibre mide bien BORDES y CARAS PLANAS, no centros ni superficies de
referencia inaccesibles. Las plantillas piden mediciones faciles de apoyar y
vos (Claude) derivas lo que necesita el CAD. Formulas de derivacion (los
nombres entre comillas son los items de las plantillas):

- ALTURAS (se miden incluyendo el PCB, contra su cara plana):
    alto real arriba (componentes) = "Alto total arriba (comp + PCB)" - "Espesor PCB"
    alto real abajo  (pines)        = "Alto total abajo (pines + PCB)" - "Espesor PCB"
  El de arriba define la altura de la tapa; el de abajo, a que altura va el
  apoyo/repisa del board dentro de la caja.

- AGUJEROS centro-a-centro (no se mide el centro, se deriva):
    centro-a-centro X = "Agujeros: span exterior X" - "Agujeros: diametro"
    centro-a-centro Y = "Agujeros: span exterior Y" - "Agujeros: diametro"
  (span exterior = borde externo de un agujero al borde externo del otro;
   formula valida para agujeros de igual diametro).

- POSICION de un agujero en la placa (desde el borde):
    centro X = "Agujero: borde placa a borde X" + "Agujeros: diametro"/2
    centro Y = "Agujero: borde placa a borde Y" + "Agujeros: diametro"/2
  ("borde placa a borde" = del borde de la placa al borde mas cercano del agujero).

- CONECTOR (USB, etc.), recorte en la pared:
    posicion horizontal del centro = "USB: borde placa a borde USB" + "USB: ancho abertura"/2
    el recorte vertical va desde "USB: cara inf PCB a base USB" y mide de alto
    "USB: alto abertura". Ojo: si el board se sostiene por el USB que pasa por
    la pared, esa posicion es critica.

Datum sugerido: esquina inferior-izquierda mirando el lado de componentes.
Clearances tipicos para imprimir: +0.5mm por lado en la cavidad; apoyo del
board = alto real abajo + margen; tapa = alto real arriba + margen.

La lista de una sesion se puede editar en vivo (agregar/quitar/renombrar
items por la web o por API) antes de confirmar.

## Solo con HTTP (sin MCP)
- GET    /api/value           -> {"mm":12.34,"counts":1234,"unit":"mm","on":true,"rel":false,"ts":...}
- GET    /api/status          -> diagnostico: modo lectura, frames ok/bad, heap, rssi, ble
- GET    /api/templates       -> plantillas para cajitas [{id,name,items[]}]
- POST   /api/session/template body {"id":"devboard"} -> inicia sesion con esa plantilla
- POST   /api/session         body {"items":["ancho","alto"]} -> crea la sesion guiada
- GET    /api/session         -> {"active","confirmed","current","allDone","items":[{"n","v","d"}]}
- POST   /api/session/select  body {"index":N} -> mover el cursor a un item (repetir)
- POST   /api/session/confirm -> confirmar (requiere todos los items medidos)
- POST   /api/session/item    body {"name":"..."} -> agregar item a la sesion
- POST   /api/session/rename  body {"index":N,"name":"..."} -> renombrar item
- POST   /api/session/remove  body {"index":N} -> quitar item
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
