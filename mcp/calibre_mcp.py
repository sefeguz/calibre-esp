# /// script
# requires-python = ">=3.10"
# dependencies = ["mcp>=1.2", "httpx"]
# ///
"""Servidor MCP para Calibre-ESP.

Expone el calibre digital (ESP32-C3) como herramientas MCP para que Claude
pueda leer mediciones en vivo y pedir mediciones guiadas: Claude llama a
esperar_captura("ancho interior"), el usuario aprieta el boton BOOT del
calibre, y el valor llega solo.

Configuracion: variable de entorno CALIBRE_URL (default http://192.168.1.43).
Ejecutar con: uv run --script calibre_mcp.py
"""

import os
import time

import httpx
from mcp.server.fastmcp import FastMCP

BASE_URL = os.environ.get("CALIBRE_URL", "http://192.168.1.43").rstrip("/")

mcp = FastMCP("calibre")


def _get(path: str, timeout: float = 5.0) -> httpx.Response:
    return httpx.get(f"{BASE_URL}{path}", timeout=timeout)


@mcp.tool()
def leer_medicion() -> dict:
    """Lee la medicion ACTUAL del calibre (instantanea, sin esperar al boton).

    Devuelve mm (float), unit (la unidad que muestra el LCD), on (si el
    calibre esta transmitiendo) y rel (si hay zero relativo activo).
    Usar para chequeos rapidos; para una sesion de mediciones guiada usar
    esperar_captura, que espera a que el usuario confirme con el boton.
    """
    r = _get("/api/value")
    r.raise_for_status()
    return r.json()


@mcp.tool()
def esperar_captura(etiqueta: str, timeout_s: float = 25.0) -> dict:
    """Espera a que el usuario capture una medicion con el boton BOOT del
    calibre (o el boton Capturar de la web) y devuelve el valor en mm.

    Flujo tipico para medir una pieza: por cada dimension que necesites,
    1) avisale al usuario QUE medir (p.ej. "medi el ancho interior"),
    2) llama a esta herramienta con esa etiqueta,
    3) el usuario posiciona el calibre y aprieta BOOT -> la herramienta
       devuelve {etiqueta, valor_mm, ok: true}.

    Si devuelve {ok: false, timeout: true} el usuario todavia no apreto el
    boton: simplemente VOLVE A LLAMARLA con la misma etiqueta para seguir
    esperando (no es un error).
    """
    try:
        r = _get("/api/captures")
        r.raise_for_status()
        n0 = len(r.json())
    except httpx.HTTPError as e:
        return {"ok": False, "error": f"no se pudo consultar el calibre: {e}"}

    deadline = time.monotonic() + min(timeout_s, 25.0)
    while time.monotonic() < deadline:
        time.sleep(0.4)
        try:
            r = _get("/api/captures")
            r.raise_for_status()
            caps = r.json()
        except httpx.HTTPError:
            continue  # blip de red: seguir esperando
        if len(caps) > n0 or (caps and len(caps) < n0):  # nueva (o ring dio vuelta)
            v = caps[-1]["v"]
            return {"ok": True, "etiqueta": etiqueta, "valor_mm": float(v)}

    return {
        "ok": False,
        "timeout": True,
        "etiqueta": etiqueta,
        "hint": "el usuario aun no apreto el boton; volver a llamar para seguir esperando",
    }


@mcp.tool()
def nueva_medicion(etiquetas: list[str]) -> dict:
    """Inicia una SESION DE MEDICION GUIADA: crea la lista de mediciones que
    necesitas (ej. ["ancho interior", "alto", "profundidad"]) y en la web del
    calibre aparece una tabla; el usuario va completando cada una con el boton
    del calibre (puede volver a cualquier fila y repetirla) y al final
    CONFIRMA para entregartelas todas juntas.

    Flujo: 1) llamar nueva_medicion con todas las etiquetas, 2) avisar al
    usuario que la lista esta en la web, 3) llamar esperar_mediciones (en
    loop si da timeout) hasta recibir los valores confirmados.

    Maximo 24 etiquetas, hasta 40 caracteres cada una.
    """
    if not etiquetas or len(etiquetas) > 24:
        return {"ok": False, "error": "entre 1 y 24 etiquetas"}
    r = httpx.post(f"{BASE_URL}/api/session",
                   json={"items": [e[:40] for e in etiquetas]}, timeout=5.0)
    if r.status_code != 200:
        return {"ok": False, "error": f"el dispositivo respondio {r.status_code}"}
    return {"ok": True, "items": len(etiquetas),
            "hint": "lista visible en la web; llamar esperar_mediciones"}


@mcp.tool()
def listar_plantillas() -> list:
    """Lista las plantillas de medicion disponibles para disenar cajitas de
    dispositivos IoT/Arduino. Cada una trae una lista de mediciones pensadas
    para ser faciles de tomar con el calibre (medir bordes, no centros).
    Devuelve [{id, name, items:[...]}]. Usar iniciar_plantilla(id) para
    arrancar una sesion con esa lista."""
    r = _get("/api/templates")
    r.raise_for_status()
    return r.json()


@mcp.tool()
def iniciar_plantilla(template_id: str) -> dict:
    """Inicia una SESION DE MEDICION GUIADA desde una plantilla (ver
    listar_plantillas para los id disponibles, p.ej. 'devboard', 'display',
    'sensor', 'panel', 'simple'). La tabla aparece en la web del calibre y el
    usuario la completa con el boton. Luego usar esperar_mediciones() para
    recibir los valores confirmados."""
    r = httpx.post(f"{BASE_URL}/api/session/template",
                   json={"id": template_id}, timeout=5.0)
    if r.status_code != 200:
        return {"ok": False, "error": f"plantilla '{template_id}' desconocida"}
    return {"ok": True, "hint": "lista visible en la web; llamar esperar_mediciones"}


@mcp.tool()
def esperar_mediciones(timeout_s: float = 25.0) -> dict:
    """Espera a que el usuario complete TODAS las mediciones de la sesion
    (creada con nueva_medicion) y las CONFIRME en la web. Devuelve
    {ok: true, mediciones: {etiqueta: valor_mm, ...}} y limpia la sesion.

    Si devuelve {ok: false, timeout: true}: el usuario sigue midiendo —
    VOLVER A LLAMARLA para seguir esperando (no es un error). El campo
    "progreso" indica cuantas van.
    """
    deadline = time.monotonic() + min(timeout_s, 25.0)
    progreso = None
    while time.monotonic() < deadline:
        try:
            r = _get("/api/session")
            r.raise_for_status()
            s = r.json()
        except httpx.HTTPError:
            time.sleep(0.5)
            continue
        if not s.get("active"):
            return {"ok": False,
                    "error": "no hay sesion activa (¿cancelada? crear otra con nueva_medicion)"}
        items = s.get("items", [])
        progreso = f"{sum(1 for i in items if i['d'])}/{len(items)}"
        if s.get("confirmed"):
            mediciones = {i["n"]: i["v"] for i in items}
            try:
                httpx.delete(f"{BASE_URL}/api/session", timeout=5.0)
            except httpx.HTTPError:
                pass
            return {"ok": True, "mediciones": mediciones}
        time.sleep(0.6)
    return {"ok": False, "timeout": True, "progreso": progreso,
            "hint": "el usuario sigue midiendo; volver a llamar para seguir esperando"}


@mcp.tool()
def cancelar_medicion() -> dict:
    """Cancela la sesion de medicion guiada activa (si la hay)."""
    r = httpx.delete(f"{BASE_URL}/api/session", timeout=5.0)
    r.raise_for_status()
    return {"ok": True}


@mcp.tool()
def estado() -> dict:
    """Diagnostico del dispositivo: modo de lectura, calibre on/off, calidad
    de frames, BLE conectado, niveles de senal (mV), heap, RSSI y uptime."""
    r = _get("/api/status")
    r.raise_for_status()
    return r.json()


@mcp.tool()
def listar_capturas() -> list:
    """Lista las mediciones ya capturadas (el log del dispositivo): valor en
    mm y antiguedad en segundos de cada una."""
    r = _get("/api/captures")
    r.raise_for_status()
    return r.json()


@mcp.tool()
def borrar_capturas() -> dict:
    """Borra el log de capturas del dispositivo. Util al empezar una sesion
    de mediciones nueva."""
    r = httpx.delete(f"{BASE_URL}/api/captures", timeout=5.0)
    r.raise_for_status()
    return {"ok": True}


@mcp.tool()
def zero_relativo() -> dict:
    """Activa/desactiva el zero relativo (medir diferencias respecto de la
    posicion actual del calibre)."""
    r = httpx.post(f"{BASE_URL}/api/zero", timeout=5.0)
    r.raise_for_status()
    return {"ok": True}


if __name__ == "__main__":
    mcp.run()
