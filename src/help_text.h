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
El calibre mide bien BORDES, no centros imaginarios. Por eso las plantillas
piden mediciones faciles de apoyar y vos (Claude) derivas lo que necesita el
CAD:
- Agujeros centro-a-centro: se mide borde externo a borde externo de los dos
  agujeros (span_ext) y el diametro (d). Centro-a-centro = span_ext - d (si
  son iguales) o span_ext - d1/2 - d2/2.
- Posicion de un agujero: "borde a centro X/Y" = distancia del borde de la
  placa al borde del agujero + radio. Da el centro en coordenadas del PCB.
- Stack en Z: "alto sobre PCB" (componente mas alto, define la tapa) +
  "espesor PCB" + "alto bajo PCB" (pines, define a que altura va el apoyo
  del board adentro de la caja).
- Conectores (USB, etc.): ancho/alto de la abertura + "centro a borde
  lateral" (posicion horizontal) + "centro sobre PCB" (posicion vertical del
  recorte en la pared). Ojo si el board se sostiene por el USB que pasa por
  la pared: esa posicion es critica.
Datum sugerido: esquina inferior-izquierda mirando el lado de componentes.
Clearances tipicos para imprimir: +0.5mm por lado en la cavidad; apoyo del
board = alto-bajo-PCB + margen; tapa = alto-sobre-PCB + margen.

## Solo con HTTP (sin MCP)
- GET    /api/value           -> {"mm":12.34,"counts":1234,"unit":"mm","on":true,"rel":false,"ts":...}
- GET    /api/status          -> diagnostico: modo lectura, frames ok/bad, heap, rssi, ble
- GET    /api/templates       -> plantillas para cajitas [{id,name,items[]}]
- POST   /api/session/template body {"id":"devboard"} -> inicia sesion con esa plantilla
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
