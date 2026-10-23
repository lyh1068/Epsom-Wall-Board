#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "data/route_parse.h"

/*
 * LRU cache of callsign -> route, including "adsbdb does not know this
 * callsign" so those are not re-asked every poll. Pure C, host-tested; the
 * caller owns the storage, locking and persistence.
 */

typedef enum {
    RC_EMPTY = 0,
    RC_FOUND,
    RC_UNKNOWN,
} rc_kind_t;

typedef struct {
    char key[9];
    uint8_t kind;           /* rc_kind_t */
    uint32_t fetched;       /* epoch seconds; 0 if the clock was not set */
    uint32_t last_used;     /* LRU clock */
    route_t route;          /* valid when kind == RC_FOUND */
} rc_entry_t;

typedef struct {
    rc_entry_t *e;
    int cap;
    uint32_t clock;
    bool dirty;             /* changed since the last serialise */
} route_cache_t;

void rc_init(route_cache_t *c, rc_entry_t *storage, int cap);

/*
 * Look up a callsign and mark it recently used. Returns NULL if absent or
 * older than its TTL. Expiry is skipped while now is before 2023 (clock not
 * yet set), so an unset clock never throws the cache away.
 */
const rc_entry_t *rc_get(route_cache_t *c, const char *key, uint32_t now,
                         uint32_t ttl_found, uint32_t ttl_unknown);

/* Insert or replace, evicting the least recently used entry when full. */
void rc_put(route_cache_t *c, const char *key, rc_kind_t kind, const route_t *route, uint32_t now);

int rc_count(const route_cache_t *c);

/* Bytes needed by rc_serialize for this cache. */
size_t rc_serialized_size(const route_cache_t *c);

/* Write the cache to buf; returns bytes written, or 0 if buf is too small. */
size_t rc_serialize(route_cache_t *c, uint8_t *buf, size_t size);

/* Load from buf, replacing the contents. Rejects data from another layout or
 * version (returns false, cache left empty). */
bool rc_deserialize(route_cache_t *c, const uint8_t *buf, size_t len);
