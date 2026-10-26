#pragma once

#include <stdbool.h>
#include <time.h>
#include "data/trains_logic.h"
#include "esp_err.h"

/* Epsom (EPS) departures from Darwin, via the Rail Data Marketplace (PLAN.md §4). */

typedef struct {
    time_t last_ok;
    int http_status;
    esp_err_t last_err;
    unsigned backoff_s;
    bool waiting_for_wifi;
    bool no_key;            /* RAIL_DATA_KEY still its placeholder: never polls */
} trains_meta_t;

void trains_start(void);

/* False until there is a board to show. meta may be NULL. */
bool trains_get(train_board_t *out, trains_meta_t *meta);
