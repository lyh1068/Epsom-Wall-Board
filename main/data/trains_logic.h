#pragma once

#include <stdbool.h>

/*
 * Departure-board logic with no I/O: Darwin time strings, the midnight wrap,
 * and the leave-by rule (PLAN.md §5.1). Host-tested. Times are minutes since
 * local midnight; the caller gets "now" from localtime(), which handles BST.
 */

#define TRAINS_MAX 10

typedef struct {
    char std[6];            /* scheduled, "HH:MM" */
    char etd[12];           /* "On time", "HH:MM", "Delayed", "Cancelled" */
    char dest[28];
    char dest_crs[4];
    char platform[4];       /* "" if not yet announced */
    char operator_name[24];
    bool london;            /* destination or a later calling point is in London */
} train_t;

typedef enum {
    ETD_ON_TIME,
    ETD_EXPECTED,           /* etd is a later "HH:MM" */
    ETD_DELAYED,            /* "Delayed", no time given */
    ETD_CANCELLED,
    ETD_UNKNOWN,
} etd_kind_t;

/* "HH:MM" -> minutes since midnight, or -1. */
int hhmm_to_min(const char *s);

/* Signed minutes from now to t, both minutes since midnight, wrapping across
 * midnight so a 00:10 train seen at 23:50 is 20 minutes away, not -1420. */
int minutes_until(int now_min, int t_min);

/* Classify etd; *expected_min gets the best known departure time (the
 * scheduled time for "On time" and "Delayed"), or -1 if cancelled/unknown. */
etd_kind_t train_etd(const train_t *t, int *expected_min);

/* Minutes late, from etd versus std; 0 unless etd is a later time. */
int train_delay_min(const train_t *t);

typedef struct {
    int index;              /* into the services array */
    int leave_in;           /* minutes until you must leave: expected - now - walk */
} leave_by_t;

/*
 * The first catchable London-bound service: not cancelled, and departing at
 * least walk_min from now, within horizon_min. A "Delayed" train with no new
 * time counts at its scheduled time, the earliest it could go. False if none.
 */
bool leave_by(const train_t *t, int n, int now_min, int walk_min, int horizon_min, leave_by_t *out);
