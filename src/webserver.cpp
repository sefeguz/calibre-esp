#include "webserver.h"
#include "config.h"
#include "app.h"
#include "settings.h"
#include "ble_keyboard.h"
#include "web_ui.h"

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
// WiFi: STA con credenciales guardadas; si falla, AP con portal cautivo.
// ---------------------------------------------------------------------------
static void wifiBegin()
{
    WiFi.setHostname(settings.deviceName.c_str());

    if (settings.wifiSsid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(settings.wifiSsid.c_str(), settings.wifiPass.c_str());
        Serial.printf("[wifi] conectando a '%s'", settings.wifiSsid.c_str());

        uint32_t deadline = millis() + WIFI_CONNECT_TIMEOUT_MS;
        while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
            delay(250);
            Serial.print('.');
        }
        Serial.println();
    }

    if (WiFi.status() == WL_CONNECTED) {
        WiFi.setAutoReconnect(true);
        Serial.printf("[wifi] conectado, IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        apMode = true;
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        dnsServer.start(53, "*", WiFi.softAPIP());  // portal cautivo
        Serial.printf("[wifi] modo AP '%s', IP: %s\n", AP_SSID,
                      WiFi.softAPIP().toString().c_str());
    }

    if (MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("[mdns] http://%s.local\n", MDNS_NAME);
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
    doc["ap"] = apMode;
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
    // UI principal (embebida en flash)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", WEB_UI_HTML);
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

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest* req) {
        JsonDocument doc;
        doc["ssid"] = settings.wifiSsid;
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
            if (o["ssid"].is<const char*>()) settings.wifiSsid = o["ssid"].as<String>();
            if (o["pass"].is<const char*>() && strlen(o["pass"]) > 0)
                settings.wifiPass = o["pass"].as<String>();
            if (o["name"].is<const char*>() && strlen(o["name"]) > 0)
                settings.deviceName = o["name"].as<String>();
            if (o["sep"].is<const char*>()) {
                const char* s = o["sep"];
                if (s[0] == ',' || s[0] == '.') settings.decimalSep = s[0];
            }
            if (o["eol"].is<int>()) settings.eolKey = (EolKey)constrain(o["eol"].as<int>(), 0, 2);
            if (o["ble"].is<bool>()) settings.bleEnabled = o["ble"];
            if (o["rmode"].is<int>()) settings.readMode = constrain(o["rmode"].as<int>(), 0, 2);
            if (o["inv"].is<bool>()) settings.invert = o["inv"];
            settings.save();
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

void wsBroadcastStatus()
{
    if (ws.count() == 0) return;
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
    if (apMode) dnsServer.processNextRequest();
    static uint32_t lastClean = 0;
    if (millis() - lastClean > 1000) {
        lastClean = millis();
        ws.cleanupClients();
    }
}
