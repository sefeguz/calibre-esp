#include "session.h"

namespace {

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
SessionItem  s_items[SESSION_MAX_ITEMS];
uint8_t      s_count = 0;
uint8_t      s_current = 0;
bool         s_active = false;
bool         s_confirmed = false;

// próximo ítem pendiente a partir de `from` (con vuelta); -1 si no hay
int nextPendingLocked(uint8_t from)
{
    for (uint8_t k = 0; k < s_count; k++) {
        uint8_t i = (from + k) % s_count;
        if (!s_items[i].done) return i;
    }
    return -1;
}

} // namespace

namespace Session {

bool start(const char* const names[], size_t n)
{
    if (n == 0 || n > SESSION_MAX_ITEMS) return false;

    portENTER_CRITICAL(&s_mux);
    s_count = (uint8_t)n;
    for (size_t i = 0; i < n; i++) {
        strncpy(s_items[i].name, names[i], SESSION_NAME_LEN);
        s_items[i].name[SESSION_NAME_LEN] = '\0';
        s_items[i].mm = 0;
        s_items[i].done = false;
    }
    s_current = 0;
    s_active = true;
    s_confirmed = false;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void cancel()
{
    portENTER_CRITICAL(&s_mux);
    s_active = false;
    s_confirmed = false;
    s_count = 0;
    portEXIT_CRITICAL(&s_mux);
}

bool isActive()    { return s_active; }
bool isConfirmed() { return s_confirmed; }

bool allDone()
{
    portENTER_CRITICAL(&s_mux);
    bool all = s_active && s_count > 0 && nextPendingLocked(0) < 0;
    portEXIT_CRITICAL(&s_mux);
    return all;
}

void select(uint8_t index)
{
    portENTER_CRITICAL(&s_mux);
    if (s_active && index < s_count) s_current = index;
    portEXIT_CRITICAL(&s_mux);
}

bool record(float mm)
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (s_active && !s_confirmed && s_count > 0) {
        s_items[s_current].mm = mm;
        s_items[s_current].done = true;
        int nxt = nextPendingLocked(s_current + 1);
        if (nxt >= 0) s_current = (uint8_t)nxt;
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

bool confirm()
{
    bool ok = false;
    portENTER_CRITICAL(&s_mux);
    if (s_active && s_count > 0 && nextPendingLocked(0) < 0) {
        s_confirmed = true;
        ok = true;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

uint8_t count()   { return s_count; }
uint8_t current() { return s_current; }

SessionItem itemAt(uint8_t i)
{
    SessionItem it = {};
    portENTER_CRITICAL(&s_mux);
    if (i < s_count) it = s_items[i];
    portEXIT_CRITICAL(&s_mux);
    return it;
}

} // namespace Session
