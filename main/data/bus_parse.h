#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Parser for TfL /StopPoint/{naptanId}/Arrivals. Pure C + cJSON, host-tested
 * against test/fixtures/tfl_bus_arrivals.json. TfL covers its own routes
 * only: Surrey County Council buses through Epsom are not in this feed.
 */

#define BUS_MAX 8
#define BUS_MAX_WAITING 8

typedef struct {
    char line[6];           /* "406", "S2" */
    char dest[28];          /* ASCII-folded */
    char vehicle[12];       /* registration, "LJ16EVN" */
    bool outbound;          /* TfL's direction, so a bus ending one trip is not
                             * mistaken for the same bus starting its next */
    bool not_left;          /* the trip has not set off: the time is TfL's
                             * timetable, not a tracked bus, and may never come */
    int secs;               /* seconds to the stop when fetched */
} bus_arrival_t;

typedef struct {
    int count;
    bus_arrival_t a[BUS_MAX];   /* soonest first */
    int nwaiting;
    bus_arrival_t waiting[BUS_MAX_WAITING]; /* every bus listed here for a trip
                                             * that begins here, not only the soonest */
    char stop_name[32];     /* "Epsom High Street"; "" when nothing is due */
    char stop_letter[4];    /* "G" */
} bus_board_t;

/* False if the JSON is not an array of predictions. TfL returns them
 * unsorted; out is sorted soonest first and keeps the first BUS_MAX.
 * starts: the routes whose trips begin at this stop ("418 S2", or ""): their
 * arrivals are marked not_left and also kept in out->waiting. */
bool bus_parse(const char *json, size_t len, const char *starts, bus_board_t *out);

/* Is a (at a later stop) a bus that start, the board of the stop where its
 * trip begins, still lists for that trip? Then it has not set off either. */
bool bus_still_waiting(const bus_board_t *start, const bus_arrival_t *a);

/*
 * A bus that has not set off usually forms its trip from the one it is still
 * finishing, into Epsom, which shows as the same vehicle in the other
 * direction. It cannot be here before it ends that trip, turns round
 * (turn_s) and covers TfL's own gap from the start stop to this one. Returns
 * that earliest time, in seconds from now, when it is later than a_secs
 * (TfL's time, counted down to now); -1 when TfL's time is possible or the
 * bus is not seen inbound. boards[j] were fetched elapsed[j] s ago.
 */
int bus_late_secs(const bus_arrival_t *a, int a_secs, const bus_board_t *boards, const int *elapsed, int n,
                  int turn_s);

/* "due", "1 min", "12 min" for a whole number of seconds still to wait. */
void bus_fmt_wait(char *buf, size_t size, int secs);
