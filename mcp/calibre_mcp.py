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
