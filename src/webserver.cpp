#include "webserver.h"
#include "config.h"
#include "app.h"
#include "settings.h"
#include "session.h"
#include "ble_keyboard.h"
#include "web_ui.h"
#include "help_text.h"

#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static DNSServer dnsServer;
static bool apMode = false;

bool webserverInApMode() { return apMode; }

// ---------------------------------------------------------------------------
// WiFi multi-red con roaming. Claves del diseño (ESP32-C3 = una sola radio):
//  - El AP "Calibre-ESP" queda SIEMPRE encendido (AP_STA). Nunca se apaga,
//    así el hotspot no "desaparece"; cuando la STA conecta solo se baja el
//    portal cautivo. El dispositivo es alcanzable por AP (192.168.4.1) y LAN.
//  - NO se escanea ni se intenta conectar la STA mientras hay un cliente en
//    el hotspot (softAPgetStationNum>0): escanear salta de canal y conectar
//    mueve el AP de canal — ambos echarían al usuario que está configurando.
//  - Escaneo de UI (botón "Buscar redes") separado del de roaming: el de UI
//    solo cachea resultados, NUNCA conecta (no tira el AP).
//  - Sin redes guardadas al alcance: re-escaneo cada 30 s (solo si el AP está
//    libre). Si se pierde la red conectada >20 s, vuelve el portal cautivo.
// ---------------------------------------------------------------------------
enum class WifiState : uint8_t { IDLE, SCANNING, CONNECTING, CONNECTED };

static WifiState wifiState = WifiState::IDLE;
static uint32_t wifiNextScanMs = 0;
static uint32_t wifiDeadlineMs = 0;
static bool     scanForUi = false;       // el escaneo en curso es solo para la UI
static bool     uiScanPending = false;   // la UI pidió un escaneo
static bool     uiScanWasConnected = false;  // había STA antes del escaneo de UI
static bool     wifiReapplyPending = false;  // releer redes y reconectar (tras guardar)
// caché del último escaneo (para el selector de redes de la UI)
static String   scanCacheJson;
static uint32_t scanCacheMs = 0;
static int8_t   wifiCand[WIFI_MAX_NETWORKS];   // índices a settings.wifi
static uint8_t  wifiCandCount = 0;
static uint8_t  wifiCandIdx = 0;
static uint32_t staLostSinceMs = 0;
static bool     mdnsUp = false;

// Slug del nombre del dispositivo para hostname/mDNS: minúsculas, solo
// [a-z0-9-]. "Calibre-ESP" -> "calibre-esp" -> http://calibre-esp.local
static String hostSlug()
{
    String s = settings.deviceName;
    s.toLowerCase();
    String out;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (isalnum((unsigned char)c)) out += c;
        else if (out.length() && out[out.length() - 1] != '-') out += '-';
    }
    while (out.length() && out[out.length() - 1] == '-') out.remove(out.length() - 1);
    if (!out.length()) out = MDNS_NAME;
    return out;
}

// Levanta el portal cautivo (DNS que redirige todo al dispositivo). El AP en
// sí ya está prendido desde wifiBegin y nunca se apaga.
static void captivePortalOn()
{
    if (apMode) return;
    apMode = true;
    dnsServer.start(53, "*", WiFi.softAPIP());
}

static void captivePortalOff()
{
    if (!apMode) return;
    apMode = false;
    dnsServer.stop();
}

static void mdnsStart()
{
    if (mdnsUp) MDNS.end();
    String host = hostSlug();
    if (MDNS.begin(host.c_str())) {
        MDNS.addService("http", "tcp", 80);
        mdnsUp = true;
        Serial.printf("[mdns] http://%s.local\n", host.c_str());
    }
}

// Serializa los resultados de un escaneo terminado a la caché JSON:
// deduplicado por SSID (conserva la de mejor señal), ordenado por RSSI.
static void wifiCacheScan(int n)
{
    struct Net { String ssid; int32_t rssi; bool sec; };
    Net nets[20];
    uint8_t nn = 0;
    for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (!ssid.length()) continue;          // redes ocultas
        int32_t rssi = WiFi.RSSI(i);
        bool dup = false;
        for (uint8_t k = 0; k < nn; k++) {
            if (nets[k].ssid == ssid) {
                dup = true;
                if (rssi > nets[k].rssi) nets[k].rssi = rssi;
                break;
            }
        }
        if (!dup && nn < 20) {
            nets[nn].ssid = ssid;
            nets[nn].rssi = rssi;
            nets[nn].sec = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
            nn++;
        }
    }
    for (uint8_t a = 0; a < nn; a++)
        for (uint8_t b = a + 1; b < nn; b++)
            if (nets[b].rssi > nets[a].rssi) {
                Net t = nets[a]; nets[a] = nets[b]; nets[b] = t;
            }

    JsonDocument doc;
    JsonArray arr = doc["networks"].to<JsonArray>();
    for (uint8_t i = 0; i < nn; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"] = nets[i].ssid;
        o["rssi"] = nets[i].rssi;
        o["sec"] = nets[i].sec;
    }
    scanCacheJson = "";
    serializeJson(doc, scanCacheJson);
    scanCacheMs = millis();
}

static void wifiTryCandidate()
{
    const WifiNet& net = settings.wifi[wifiCand[wifiCandIdx]];
    Serial.printf("[wifi] intentando '%s' (%u/%u)...\n", net.ssid.c_str(),
                  wifiCandIdx + 1, wifiCandCount);
    WiFi.begin(net.ssid.c_str(), net.pass.c_str());
    wifiDeadlineMs = millis() + 12000;
    wifiState = WifiState::CONNECTING;
}

static void wifiBegin()
{
    WiFi.setHostname(hostSlug().c_str());
    WiFi.persistent(false);
    WiFi.setAutoReconnect(true);

    // AP siempre encendido (no se apaga nunca) + portal cautivo
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
    apMode = true;
    dnsServer.start(53, "*", WiFi.softAPIP());
    Serial.printf("[wifi] AP '%s' listo, IP: %s\n", AP_SSID,
                  WiFi.softAPIP().toString().c_str());

    wifiState = WifiState::IDLE;
    wifiNextScanMs = millis();      // escanear ya mismo
}

static void wifiTick()
{
    uint32_t now = millis();
    bool staUp = WiFi.status() == WL_CONNECTED;
    bool apBusy = WiFi.softAPgetStationNum() > 0;   // alguien usando el hotspot

    // Reaplicar redes tras un guardado: soltar la STA y re-evaluar para
    // conectar a la mejor guardada (la respuesta HTTP ya se envió, así que
    // ahora es seguro cortar la STA). Se da ~1.2 s para que el cliente reciba.
    if (wifiReapplyPending) {
        static uint32_t reapplyAt = 0;
        if (reapplyAt == 0) { reapplyAt = now + 1200; }
        else if (now >= reapplyAt) {
            reapplyAt = 0;
            wifiReapplyPending = false;
            if (staUp) WiFi.disconnect();
            wifiNextScanMs = now;
            wifiState = WifiState::IDLE;
            return;
        }
    }
    (void)apBusy;

    // Escaneo de UI pendiente: lanzable desde IDLE o CONNECTED (no a mitad de
    // un escaneo/conexión). Solo cachea resultados, NUNCA conecta. Un escaneo
    // estando conectado puede tirar la STA un instante (una sola radio): se
    // recuerda para reconectar enseguida al terminar.
    if (uiScanPending && (wifiState == WifiState::IDLE ||
                          wifiState == WifiState::CONNECTED)) {
        uiScanPending = false;
        scanForUi = true;
        uiScanWasConnected = staUp;
        WiFi.scanNetworks(true);
        wifiState = WifiState::SCANNING;
        return;
    }

    switch (wifiState) {
        case WifiState::IDLE: {
            // Escaneo de roaming: solo si el AP está libre y aún no conectados.
            // Roamear (escanear + conectar a la guardada con mejor señal) aun
            // si hay un cliente en el hotspot: como el AP nunca se apaga, lo
            // peor es un parpadeo del AP al cambiar de canal — NO bloquearlo,
            // si no un celular pegado al hotspot impediría que el equipo se
            // conecte a la red que el usuario acaba de configurar.
            bool roam = settings.wifiCount > 0 && !staUp && now >= wifiNextScanMs;
            if (roam) {
                scanForUi = false;
                WiFi.scanNetworks(true);
                wifiState = WifiState::SCANNING;
            } else if (staUp) {
                wifiState = WifiState::CONNECTED;   // ya conectado (p.ej. tras boot)
            }
            break;
        }

        case WifiState::SCANNING: {
            int n = WiFi.scanComplete();
            if (n == WIFI_SCAN_RUNNING) return;
            if (n >= 0) wifiCacheScan(n);   // alimentar el selector de la UI

            wifiCandCount = 0;
            if (n > 0 && !scanForUi) {
                // candidatas = redes guardadas presentes, por señal descendente
                struct { int8_t idx; int32_t rssi; } found[WIFI_MAX_NETWORKS];
                uint8_t nf = 0;
                for (int i = 0; i < n; i++) {
                    String ssid = WiFi.SSID(i);
                    for (uint8_t k = 0; k < settings.wifiCount; k++) {
                        if (settings.wifi[k].ssid != ssid) continue;
                        bool dup = false;
                        for (uint8_t f = 0; f < nf; f++)
                            if (found[f].idx == (int8_t)k) { dup = true; break; }
                        if (!dup && nf < WIFI_MAX_NETWORKS) {
                            found[nf].idx = k;
                            found[nf].rssi = WiFi.RSSI(i);
                            nf++;
                        }
                    }
                }
                for (uint8_t a = 0; a < nf; a++)
                    for (uint8_t b = a + 1; b < nf; b++)
                        if (found[b].rssi > found[a].rssi) {
                            auto t = found[a]; found[a] = found[b]; found[b] = t;
                        }
                for (uint8_t a = 0; a < nf; a++) wifiCand[a] = found[a].idx;
                wifiCandCount = nf;
            }
            WiFi.scanDelete();

            // escaneo de UI: NO conectar candidatas (no tirar el AP). Pero si
            // el escaneo cortó la STA que teníamos, reconectar YA.
            if (scanForUi) {
                scanForUi = false;
                if (uiScanWasConnected && !staUp) {
                    WiFi.reconnect();
                    staLostSinceMs = now;
                    wifiState = WifiState::CONNECTED;   // su lógica reconecta
                } else {
                    wifiState = staUp ? WifiState::CONNECTED : WifiState::IDLE;
                    wifiNextScanMs = now + 30000;
                }
            } else if (wifiCandCount > 0) {
                wifiCandIdx = 0;
                wifiTryCandidate();             // conectar (AP sigue prendido)
            } else {
                wifiNextScanMs = now + 30000;   // nada conocido: reintentar
                wifiState = WifiState::IDLE;
            }
            break;
        }

        case WifiState::CONNECTING:
            if (staUp) {
                captivePortalOff();             // baja el portal; AP sigue prendido
                staLostSinceMs = 0;
                Serial.printf("[wifi] conectado a '%s', IP: %s\n",
                              WiFi.SSID().c_str(),
                              WiFi.localIP().toString().c_str());
                mdnsStart();
                wifiState = WifiState::CONNECTED;
            } else if (now >= wifiDeadlineMs) {
                wifiCandIdx++;
                if (wifiCandIdx < wifiCandCount) {
                    wifiTryCandidate();         // probar la siguiente
                } else {
                    WiFi.disconnect();
                    wifiNextScanMs = now + 30000;
                    wifiState = WifiState::IDLE;
                }
            }
            break;

        case WifiState::CONNECTED: {
            static uint32_t lastReconnectNudge = 0;
            if (staUp) {
                staLostSinceMs = 0;
                if (apMode) captivePortalOff();   // por si volvió tras un blip
            } else {
                if (staLostSinceMs == 0) {
                    staLostSinceMs = now;
                    WiFi.reconnect();             // nudge inmediato
                    lastReconnectNudge = now;
                } else if (now - lastReconnectNudge > 5000) {
                    WiFi.reconnect();             // reintento activo cada 5 s
                    lastReconnectNudge = now;
                }
                if (now - staLostSinceMs > 25000) {
                    // realmente fuera de alcance: portal cautivo + re-escaneo
                    Serial.println("[wifi] red perdida — portal cautivo de vuelta");
                    staLostSinceMs = 0;
                    captivePortalOn();
                    wifiNextScanMs = now + 3000;
                    wifiState = WifiState::IDLE;
                }
            }
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------------
static void buildStatusJson(JsonDocument& doc)
{
    CaliperDiag d = caliper.diag();
    doc["fw"] = FIRMWARE_VERSION;
    doc["on"] = d.on;
    doc["mode"] = (uint8_t)d.mode;
    doc["framesOk"] = d.framesOk;
    doc["framesBad"] = d.framesBad;
    doc["lastBits"] = d.lastFrameBits;
    char rawHex[20];
    snprintf(rawHex, sizeof(rawHex), "0x%llX", (unsigned long long)d.lastFrameRaw);
    doc["lastRaw"] = rawHex;
    doc["clkMv"] = d.clkMv;
    doc["dataMv"] = d.dataMv;
    doc["ble"] = bleKeyboard.isConnected();
    doc["rel"] = appRelativeActive();
    doc["ap"] = apMode;               // portal cautivo activo
    doc["apOn"] = true;               // el SoftAP siempre está prendido
    doc["apClients"] = WiFi.softAPgetStationNum();
    doc["rssi"] = WiFi.RSSI();
    doc["heap"] = ESP.getFreeHeap();
    doc["minHeap"] = ESP.getMinFreeHeap();
    doc["uptime"] = millis() / 1000;
}

// ---------------------------------------------------------------------------
// Rutas
// ---------------------------------------------------------------------------
static const char OTA_FORM[] PROGMEM = R"(<!DOCTYPE html><html><body style="font-family:sans-serif;background:#14161a;color:#eee;text-align:center;padding-top:40px">
<h2>Calibre-ESP &mdash; OTA</h2>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin"><br><br>
<input type="submit" value="Actualizar firmware" style="padding:10px 20px">
</form><p><a href="/" style="color:#ff8b1f">&larr; volver</a></p></body></html>)";

static void setupRoutes()
{
    // UI principal — servir DIRECTO desde flash, sin copia: send(..., String)
    // duplicaría ~28 KB en heap por request y bajo concurrencia (WS + varios
    // clientes) estrangula la RAM y corrompe respuestas.
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html",
                  (const uint8_t*)WEB_UI_HTML, strlen(WEB_UI_HTML));
    });

    // Guía para asistentes IA (estándar llms.txt): cualquier LLM con acceso
    // HTTP puede autodescubrir la API y el flujo de medición guiada.
    server.on("/llms.txt", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/plain; charset=utf-8",
                  (const uint8_t*)LLMS_TXT, strlen(LLMS_TXT));
    });

    // --- API ---
    server.on("/api/value", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        CaliperReading r = caliper.lastAtomic();   // un solo snapshot consistente
        doc["mm"] = serialized(String(appDisplayedMmFrom(r), 3));
        doc["counts"] = r.counts;
        doc["unit"] = r.unit == CaliperUnit::INCH ? "in" : "mm";
        doc["on"] = caliper.isOn();
        doc["rel"] = appRelativeActive();
        doc["ts"] = r.timestamp;
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        buildStatusJson(doc);
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // Este handler corre en la tarea AsyncTCP: las acciones que mutan estado
    // se piden con appRequest*() y las ejecuta loop() (ver main.cpp).
    server.on("/api/capture", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool ok = caliper.isOn() && caliper.hasReading();
        if (ok) appRequestCapture();
        req->send(ok ? 200 : 409, "application/json",
                  ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"sin lectura\"}");
    });

    server.on("/api/zero", HTTP_POST, [](AsyncWebServerRequest* req) {
        appRequestZero();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/redetect", HTTP_POST, [](AsyncWebServerRequest* req) {
        caliper.redetect();   // solo marca un flag; poll() la aplica en el loop
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/captures", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        uint32_t now = millis();
        size_t n = appCaptureCount();
        for (size_t i = 0; i < n; i++) {
            Capture c = appCaptureAt(i);
            JsonObject o = arr.add<JsonObject>();
            o["v"] = serialized(String(c.value_mm, 3));
            o["age"] = (now - c.ts) / 1000;   // segundos desde la captura
            o["ts"] = c.ts;
        }
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    server.on("/api/captures", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        appRequestClearCaptures();   // lo aplica loop()
        req->send(200, "application/json", "{\"ok\":true}");
    });

    // CSV pensado para Excel ES: si el separador decimal es coma, los campos
    // van con punto y coma.
    server.on("/api/captures.csv", HTTP_GET, [](AsyncWebServerRequest* req) {
        char fieldSep = settings.decimalSep == ',' ? ';' : ',';
        String csv;
        csv.reserve(appCaptureCount() * 24 + 32);
        csv += "n";
        csv += fieldSep;
        csv += "segundos";
        csv += fieldSep;
        csv += "mm\r\n";
        uint32_t now = millis();
        size_t n = appCaptureCount();
        for (size_t i = 0; i < n; i++) {
            Capture c = appCaptureAt(i);
            String v(c.value_mm, 3);
            if (settings.decimalSep == ',') v.replace('.', ',');
            csv += String(i + 1);
            csv += fieldSep;
            csv += String((now - c.ts) / 1000);
            csv += fieldSep;
            csv += v;
            csv += "\r\n";
        }
        AsyncWebServerResponse* res = req->beginResponse(200, "text/csv", csv);
        res->addHeader("Content-Disposition", "attachment; filename=capturas.csv");
        req->send(res);
    });

    // --- sesión de medición guiada ---
    server.on("/api/session", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["active"] = Session::isActive();
        if (Session::isActive()) {
            doc["confirmed"] = Session::isConfirmed();
            doc["current"] = Session::current();
            doc["allDone"] = Session::allDone();
            JsonArray arr = doc["items"].to<JsonArray>();
            uint8_t n = Session::count();
            for (uint8_t i = 0; i < n; i++) {
                SessionItem it = Session::itemAt(i);
                JsonObject o = arr.add<JsonObject>();
                o["n"] = it.name;
                if (it.done) o["v"] = serialized(String(it.mm, 2));
                else         o["v"] = nullptr;
                o["d"] = it.done;
            }
        }
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    // OJO con el orden: AsyncCallbackJsonWebHandler matchea por PREFIJO de
    // URL, así que las rutas más específicas (/select, /confirm) tienen que
    // registrarse ANTES que el handler de creación de "/api/session".
    auto* selectHandler = new AsyncCallbackJsonWebHandler(
        "/api/session/select", [](AsyncWebServerRequest* req, JsonVariant& json) {
            int idx = json["index"] | -1;
            if (Session::isActive() && idx >= 0 && idx < Session::count()) {
                Session::select((uint8_t)idx);
                wsBroadcastSession();
                req->send(200, "application/json", "{\"ok\":true}");
            } else {
                req->send(400, "application/json", "{\"ok\":false}");
            }
        });
    server.addHandler(selectHandler);

    server.on("/api/session/confirm", HTTP_POST, [](AsyncWebServerRequest* req) {
        bool ok = Session::confirm();
        if (ok) wsBroadcastSession();
        req->send(ok ? 200 : 409, "application/json",
                  ok ? "{\"ok\":true}"
                     : "{\"ok\":false,\"error\":\"faltan mediciones\"}");
    });

    server.on("/api/session", HTTP_DELETE, [](AsyncWebServerRequest* req) {
        Session::cancel();
        wsBroadcastSession();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    auto* sessionHandler = new AsyncCallbackJsonWebHandler(
        "/api/session", [](AsyncWebServerRequest* req, JsonVariant& json) {
            JsonArray items = json["items"].as<JsonArray>();
            if (items.isNull() || items.size() == 0 ||
                items.size() > SESSION_MAX_ITEMS) {
                req->send(400, "application/json",
                          "{\"ok\":false,\"error\":\"cantidad de items invalida\"}");
                return;
            }
            const char* names[SESSION_MAX_ITEMS];
            size_t n = 0;
            for (JsonVariant v : items) {
                const char* s = v.as<const char*>();
                if (s && strlen(s) > 0) names[n++] = s;
            }
            bool ok = n > 0 && Session::start(names, n);
            if (ok) wsBroadcastSession();
            req->send(ok ? 200 : 400, "application/json",
                      ok ? "{\"ok\":true}" : "{\"ok\":false}");
        });
    server.addHandler(sessionHandler);

    // Escaneo de redes para el selector de la UI. Devuelve la caché si es
    // fresca; si no, dispara un escaneo (compartido con la máquina de estados
    // cuando está en modo AP) y el cliente repite hasta tener resultados.
    server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest* req) {
        // caché fresca: devolverla (no re-escanear, no molestar al AP)
        if (scanCacheMs && millis() - scanCacheMs < 15000) {
            req->send(200, "application/json", scanCacheJson);
            return;
        }
        // pedir un escaneo de UI: el ciclo lo hace y SOLO cachea (no conecta,
        // no tira el AP). Funciona igual en modo hotspot o conectado.
        uiScanPending = true;
        req->send(200, "application/json", "{\"scanning\":true}");
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        JsonArray nets = doc["networks"].to<JsonArray>();
        for (uint8_t i = 0; i < settings.wifiCount; i++) {
            nets.add(settings.wifi[i].ssid);   // sin passwords
        }
        doc["name"] = settings.deviceName;
        doc["sep"] = String(settings.decimalSep);
        doc["eol"] = (uint8_t)settings.eolKey;
        doc["ble"] = settings.bleEnabled;
        doc["rmode"] = settings.readMode;
        doc["inv"] = settings.invert;
        doc["fw"] = FIRMWARE_VERSION;
        String out;
        serializeJson(doc, out);
        req->send(200, "application/json", out);
    });

    auto* cfgHandler = new AsyncCallbackJsonWebHandler(
        "/api/config", [](AsyncWebServerRequest* req, JsonVariant& json) {
            JsonObject o = json.as<JsonObject>();
            bool wifiChanged = false;
            if (o["wifi"].is<JsonArray>()) {
                wifiChanged = true;
                // lista completa de redes; pass vacía = conservar la guardada
                JsonArray arr = o["wifi"].as<JsonArray>();
                WifiNet nuevo[WIFI_MAX_NETWORKS];
                uint8_t n = 0;
                for (JsonObject w : arr) {
                    if (n >= WIFI_MAX_NETWORKS) break;
                    const char* ssid = w["ssid"];
                    if (!ssid || !strlen(ssid)) continue;
                    const char* pass = w["pass"];
                    nuevo[n].ssid = ssid;
                    nuevo[n].pass = (pass && strlen(pass))
                                        ? String(pass)
                                        : settings.passFor(ssid);
                    n++;
                }
                for (uint8_t i = 0; i < n; i++) settings.wifi[i] = nuevo[i];
                settings.wifiCount = n;
            }
            if (o["name"].is<const char*>() && strlen(o["name"]) > 0)
                settings.deviceName = o["name"].as<String>();
            if (o["sep"].is<const char*>()) {
                const char* s = o["sep"];
                if (s[0] == ',' || s[0] == '.') settings.decimalSep = s[0];
            }
            if (o["eol"].is<int>()) settings.eolKey = (EolKey)constrain(o["eol"].as<int>(), 0, 3);
            if (o["ble"].is<bool>()) settings.bleEnabled = o["ble"];
            if (o["rmode"].is<int>()) settings.readMode = constrain(o["rmode"].as<int>(), 0, 2);
            if (o["inv"].is<bool>()) settings.invert = o["inv"];
            settings.save();
            // si cambió la lista de redes, reconectar a la mejor guardada —
            // pero NO acá (desconectar la STA cortaría esta misma respuesta
            // HTTP): se difiere a wifiTick vía flag, que corre tras enviar.
            if (wifiChanged) wifiReapplyPending = true;
            req->send(200, "application/json", "{\"ok\":true}");
        });
    server.addHandler(cfgHandler);

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", "{\"ok\":true}");
        // reiniciar después de despachar la respuesta
        xTaskCreate([](void*) {
            vTaskDelay(pdMS_TO_TICKS(400));
            ESP.restart();
        }, "reboot", 2048, nullptr, 1, nullptr);
    });

    // --- OTA ---
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", OTA_FORM);
    });
    server.on("/update", HTTP_POST,
        [](AsyncWebServerRequest* req) {
            bool ok = !Update.hasError();
            AsyncWebServerResponse* res = req->beginResponse(
                ok ? 200 : 500, "text/plain", ok ? "OK - reiniciando" : "FALLO");
            res->addHeader("Connection", "close");
            req->send(res);
            if (ok) {
                xTaskCreate([](void*) {
                    vTaskDelay(pdMS_TO_TICKS(400));
                    ESP.restart();
                }, "reboot", 2048, nullptr, 1, nullptr);
            }
        },
        [](AsyncWebServerRequest* req, String filename, size_t index,
           uint8_t* data, size_t len, bool final) {
            if (index == 0) {
                Serial.printf("[ota] inicio: %s\n", filename.c_str());
                if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
            }
            if (len && Update.write(data, len) != len) Update.printError(Serial);
            if (final) {
                if (Update.end(true)) Serial.printf("[ota] ok, %u bytes\n", index + len);
                else Update.printError(Serial);
            }
        });

    // Portal cautivo: cualquier ruta desconocida redirige a la UI
    server.onNotFound([](AsyncWebServerRequest* req) {
        if (apMode) {
            req->redirect(String("http://") + WiFi.softAPIP().toString() + "/");
        } else {
            req->send(404, "text/plain", "404");
        }
    });
}

// ---------------------------------------------------------------------------
// WebSocket
// ---------------------------------------------------------------------------
static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
                      AwsEventType type, void*, uint8_t*, size_t)
{
    if (type == WS_EVT_CONNECT) {
        // estado inicial para el cliente nuevo
        JsonDocument doc;
        doc["t"] = "st";
        doc["ble"] = bleKeyboard.isConnected();
        doc["mode"] = (uint8_t)caliper.mode();
        doc["rel"] = appRelativeActive();
        doc["on"] = caliper.isOn();
        String out;
        serializeJson(doc, out);
        client->text(out);
    }
}

void wsBroadcastReading(const CaliperReading& r, float displayedMm, bool relActive)
{
    if (ws.count() == 0) return;
    // Si algún cliente tiene la cola llena, saltear: con WiFi en power-save
    // (coexistencia BLE) la radio drena lento y encolar de más genera un
    // backlog de segundos. Mejor perder un frame viejo que mostrar atraso.
    if (!ws.availableForWriteAll()) return;
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"r\",\"v\":%.3f,\"u\":%d,\"on\":true,\"rel\":%s}",
             displayedMm, (int)r.unit, relActive ? "true" : "false");
    ws.textAll(buf);
}

void wsBroadcastCapture(float displayedMm)
{
    if (ws.count() == 0) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "{\"t\":\"cap\",\"v\":%.3f}", displayedMm);
    ws.textAll(buf);
}

void wsBroadcastSession()
{
    if (ws.count() == 0) return;
    ws.textAll("{\"t\":\"ses\"}");   // los clientes refrescan /api/session
}

void wsBroadcastStatus()
{
    if (ws.count() == 0) return;
    if (!ws.availableForWriteAll()) return;   // ver wsBroadcastReading
    char buf[96];
    snprintf(buf, sizeof(buf),
             "{\"t\":\"st\",\"ble\":%s,\"mode\":%d,\"rel\":%s,\"on\":%s}",
             bleKeyboard.isConnected() ? "true" : "false", (int)caliper.mode(),
             appRelativeActive() ? "true" : "false",
             caliper.isOn() ? "true" : "false");
    ws.textAll(buf);
}

// ---------------------------------------------------------------------------

void webserverBegin()
{
    wifiBegin();
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    setupRoutes();
    server.begin();
    Serial.println("[web] servidor iniciado en puerto 80");
}

void webserverLoop()
{
    wifiTick();
    if (apMode) dnsServer.processNextRequest();
    static uint32_t lastClean = 0;
    if (millis() - lastClean > 1000) {
        lastClean = millis();
        ws.cleanupClients();
    }
}
