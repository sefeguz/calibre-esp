// Sesión de medición guiada (web/MCP): una lista de mediciones con nombre que
// el usuario completa apretando el botón; puede volver a cualquier ítem y
// repetirlo; al confirmar, el MCP entrega todo junto a Claude.
//
// Thread-safe (mux interno): se crea/selecciona/confirma desde los handlers
// async y se completa desde loop() (botón).
#pragma once

#include <Arduino.h>

#define SESSION_MAX_ITEMS 24
#define SESSION_NAME_LEN  40

struct SessionItem {
    char  name[SESSION_NAME_LEN + 1];
    float mm;      // válido solo si done
    bool  done;
};

namespace Session {

// Crea una sesión nueva (reemplaza la anterior). Devuelve false si n inválido.
bool start(const char* const names[], size_t n);
void cancel();

bool isActive();
bool isConfirmed();
bool allDone();

// Vuelve el cursor a un ítem (para repetir su medición).
void select(uint8_t index);

// Registra la medición en el ítem actual y avanza al próximo pendiente.
// Devuelve false si no hay sesión activa o ya está confirmada.
bool record(float mm);

// Marca confirmada (solo si allDone). El MCP la recoge y la borra.
bool confirm();

// Edición de la lista durante la sesión (solo si activa y no confirmada).
bool addItem(const char* name);             // agrega un ítem pendiente al final
bool removeItem(uint8_t index);             // quita un ítem (reacomoda el cursor)
bool renameItem(uint8_t index, const char* name);

// snapshot para armar JSON (copias consistentes)
uint8_t     count();
uint8_t     current();
SessionItem itemAt(uint8_t i);

} // namespace Session
