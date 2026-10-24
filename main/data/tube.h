#pragma once

#include <stdbool.h>

/* TfL line status for the onward journey. Demo data until Phase 7. */

#define TUBE_MAX_LINES 6

typedef enum {
    TUBE_GOOD,
    TUBE_MINOR,             /* minor delays */
    TUBE_SEVERE,            /* severe delays, suspended, part closure */
    TUBE_UNKNOWN,
} tube_severity_t;

typedef struct {
    char name[24];
    tube_severity_t severity;
    char status[32];        /* "Good Service", "Minor Delays", ... */
    char reason[160];
} tube_line_t;

typedef struct {
    int count;
    tube_line_t line[TUBE_MAX_LINES];
    bool demo;
} tube_status_t;

bool tube_get(tube_status_t *out);
