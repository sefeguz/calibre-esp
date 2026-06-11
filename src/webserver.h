// WiFi (STA con fallback a AP + portal cautivo), mDNS, AsyncWebServer:
// UI embebida, WebSocket /ws, REST /api/*, OTA /update.
#pragma once

#include <Arduino.h>
#include "caliper.h"

void webserverBegin();
void webserverLoop();                       // DNS del portal cautivo + limpieza WS
bool webserverInApMode();
void webserverSetWifi(bool on);             // prender/apagar WiFi en runtime
bool webserverWifiEnabled();
void webserverNoteActivity();               // resetea el timer de auto-apagado
String webserverWifiInfo();                 // resumen STA/AP para diagnóstico serial

void wsBroadcastReading(const CaliperReading& r, float displayedMm, bool relActive);
void wsBroadcastCapture(float displayedMm);
void wsBroadcastStatus();                   // BLE/modo/rel (periódico)
void wsBroadcastSession();                  // "ses": los clientes refrescan /api/session
