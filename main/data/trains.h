#pragma once

#include <stdbool.h>
#include <time.h>
#include "data/trains_logic.h"

/* Epsom (EPS) departures. Demo data until Phase 6 (TRAINS_DEMO in config.h). */

typedef struct {
    int count;
    train_t t[TRAINS_MAX];
    char nrcc[160];         /* disruption message, "" if none */
    bool demo;
    time_t updated;
} train_board_t;

/* False until there is a board to show. */
bool trains_get(train_board_t *out);
