#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "data/bus_parse.h"
#include "data/bus_stops.h"
#include "esp_err.h"

/*
 * Bus arrivals at every TfL stop in data/bus_stops.h, polled round-robin on
 * one task. TfL has no multi-stop arrivals call, and stop-area ids return
 * nothing, so it is one request per stop.
 */

typedef struct {
    int64_t fetched_us;     /* esp_timer time of this stop's board, for counting down; 0 if never */
    bool have;
} bus_stop_meta_t;

typedef struct {
    time_t last_ok;         /* any stop */
    int http_status;        /* last request */
    esp_err_t last_err;
    unsigned backoff_s;
    bool waiting_for_wifi;
    bool keyless;
    int oldest_s;           /* age of the stalest stop board, -1 until all have data */
} bus_meta_t;

void bus_start(void);

/* Copy one stop's board (index into BUS_STOPS). False if it has no data yet. */
bool bus_get_stop(int i, bus_board_t *out, bus_stop_meta_t *meta);

void bus_get_meta(bus_meta_t *meta);
