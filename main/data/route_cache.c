#include "route_cache.h"

#include <string.h>

#define RC_MAGIC        0x31435452u     /* "RTC1" */
#define CLOCK_VALID     1672531200u     /* 2023-01-01 */

typedef struct {
    uint32_t magic;
    uint32_t entry_size;    /* layout check: a struct change invalidates old files */
    uint32_t count;
    uint32_t clock;
} rc_header_t;

void rc_init(route_cache_t *c, rc_entry_t *storage, int cap)
{
    memset(storage, 0, sizeof(*storage) * cap);
    c->e = storage;
    c->cap = cap;
    c->clock = 0;
    c->dirty = false;
}

static rc_entry_t *find(route_cache_t *c, const char *key)
{
    for (int i = 0; i < c->cap; i++) {
        if (c->e[i].kind != RC_EMPTY && strcmp(c->e[i].key, key) == 0) {
            return &c->e[i];
        }
    }
    return NULL;
}

const rc_entry_t *rc_get(route_cache_t *c, const char *key, uint32_t now,
                         uint32_t ttl_found, uint32_t ttl_unknown)
{
    rc_entry_t *e = find(c, key);
    if (e == NULL) {
        return NULL;
    }
    uint32_t ttl = e->kind == RC_FOUND ? ttl_found : ttl_unknown;
    if (now >= CLOCK_VALID && (e->fetched < CLOCK_VALID || now - e->fetched > ttl)) {
        return NULL;    /* stale, or fetched before the clock was set */
    }
    e->last_used = ++c->clock;
    return e;
}

void rc_put(route_cache_t *c, const char *key, rc_kind_t kind, const route_t *route, uint32_t now)
{
    rc_entry_t *e = find(c, key);
    if (e == NULL) {
        e = &c->e[0];
        for (int i = 0; i < c->cap; i++) {
            if (c->e[i].kind == RC_EMPTY) {
                e = &c->e[i];
                break;
            }
            if (c->e[i].last_used < e->last_used) {
                e = &c->e[i];
            }
        }
    }
    memset(e, 0, sizeof(*e));
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->kind = (uint8_t)kind;
    e->fetched = now;
    e->last_used = ++c->clock;
    if (kind == RC_FOUND && route != NULL) {
        e->route = *route;
    }
    c->dirty = true;
}

int rc_count(const route_cache_t *c)
{
    int n = 0;
    for (int i = 0; i < c->cap; i++) {
        n += c->e[i].kind != RC_EMPTY;
    }
    return n;
}

size_t rc_serialized_size(const route_cache_t *c)
{
    return sizeof(rc_header_t) + (size_t)rc_count(c) * sizeof(rc_entry_t);
}

size_t rc_serialize(route_cache_t *c, uint8_t *buf, size_t size)
{
    size_t need = rc_serialized_size(c);
    if (size < need) {
        return 0;
    }
    rc_header_t h = { RC_MAGIC, sizeof(rc_entry_t), (uint32_t)rc_count(c), c->clock };
    memcpy(buf, &h, sizeof(h));
    size_t off = sizeof(h);
    for (int i = 0; i < c->cap; i++) {
        if (c->e[i].kind != RC_EMPTY) {
            memcpy(buf + off, &c->e[i], sizeof(rc_entry_t));
            off += sizeof(rc_entry_t);
        }
    }
    c->dirty = false;
    return off;
}

bool rc_deserialize(route_cache_t *c, const uint8_t *buf, size_t len)
{
    rc_init(c, c->e, c->cap);
    rc_header_t h;
    if (len < sizeof(h)) {
        return false;
    }
    memcpy(&h, buf, sizeof(h));
    if (h.magic != RC_MAGIC || h.entry_size != sizeof(rc_entry_t)
        || len != sizeof(h) + (size_t)h.count * sizeof(rc_entry_t)) {
        return false;
    }
    int n = (int)h.count < c->cap ? (int)h.count : c->cap;
    for (int i = 0; i < n; i++) {
        rc_entry_t *e = &c->e[i];
        memcpy(e, buf + sizeof(h) + (size_t)i * sizeof(rc_entry_t), sizeof(*e));
        /* Never trust a file's terminators. */
#define TERMINATE(f) ((f)[sizeof(f) - 1] = '\0')
        TERMINATE(e->key);
        TERMINATE(e->route.callsign);
        TERMINATE(e->route.airline_icao);
        TERMINATE(e->route.airline_name);
        TERMINATE(e->route.origin_iata);
        TERMINATE(e->route.origin_icao);
        TERMINATE(e->route.origin_city);
        TERMINATE(e->route.dest_iata);
        TERMINATE(e->route.dest_icao);
        TERMINATE(e->route.dest_city);
#undef TERMINATE
        if (e->kind != RC_FOUND && e->kind != RC_UNKNOWN) {
            e->kind = RC_EMPTY;
        }
        if (e->last_used > c->clock) {
            c->clock = e->last_used;
        }
    }
    if (h.clock > c->clock) {
        c->clock = h.clock;
    }
    return true;
}
