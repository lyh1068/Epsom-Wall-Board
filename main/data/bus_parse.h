#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Parser for TfL /StopPoint/{naptanId}/Arrivals. Pure C + cJSON, host-tested
 * against test/fixtures/tfl_bus_arrivals.json. TfL covers its own routes
 * only: Surrey County Council buses through Epsom are not in this feed.
 */

#define BUS_MAX 8

typedef struct {
    char line[6];           /* "406", "S2" */
    char dest[28];          /* ASCII-folded */
    int secs;               /* seconds to the stop when fetched */
} bus_arrival_t;

typedef struct {
    int count;
    bus_arrival_t a[BUS_MAX];   /* soonest first */
    char stop_name[32];     /* "Epsom High Street"; "" when nothing is due */
    char stop_letter[4];    /* "G" */
} bus_board_t;

/* False if the JSON is not an array of predictions. TfL returns them
 * unsorted; out is sorted soonest first and keeps the first BUS_MAX. */
bool bus_parse(const char *json, size_t len, bus_board_t *out);

/* "due", "1 min", "12 min" for a whole number of seconds still to wait. */
void bus_fmt_wait(char *buf, size_t size, int secs);
