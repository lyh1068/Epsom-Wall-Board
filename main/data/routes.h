#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "data/route_parse.h"
#include "esp_err.h"

/*
 * Flight routes from adsbdb, looked up on demand and cached (LRU, persisted to
 * LittleFS). routes_lookup() never blocks: it answers from the cache or queues
 * a fetch on the routes task.
 */

typedef enum {
    ROUTE_ST_NO_CALLSIGN,   /* nothing to look up */
    ROUTE_ST_PENDING,       /* queued or in flight */
    ROUTE_ST_FOUND,
    ROUTE_ST_UNKNOWN,       /* adsbdb has no route for this callsign */
    ROUTE_ST_ERROR,         /* lookup failed; retried after a backoff */
} route_state_t;

typedef struct {
    bool fs_mounted;
    esp_err_t fs_err;       /* mount/load failure: cache is RAM-only */
    bool loaded;            /* a saved cache was read at boot */
    int cached;
    int http_status;        /* last adsbdb status, 0 if none or transport failed */
    esp_err_t last_err;
    uint32_t backoff_s;
    bool rate_limited;      /* last failure was HTTP 429: busy, not broken */
    int64_t retry_at_us;    /* esp_timer time the backoff ends */
    time_t last_save;
} routes_meta_t;

void routes_start(void);

route_state_t routes_lookup(const char *callsign, route_t *out);

void routes_get_meta(routes_meta_t *m);
