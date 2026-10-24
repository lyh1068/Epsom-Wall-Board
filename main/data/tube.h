#pragma once

#include <stdbool.h>
#include <time.h>
#include "data/tube_types.h"
#include "esp_err.h"

/* TfL line status for the onward journey (PLAN.md §4, §5.1). */

typedef struct {
    time_t last_ok;
    int http_status;
    esp_err_t last_err;
    unsigned backoff_s;
    bool waiting_for_wifi;
    bool keyless;           /* no TFL_APP_KEY: anonymous, lower rate limit */
} tube_meta_t;

void tube_start(void);

/* False until there is something to show. meta may be NULL. */
bool tube_get(tube_status_t *out, tube_meta_t *meta);
