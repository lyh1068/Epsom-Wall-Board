#pragma once

#include <stdbool.h>

/* TfL line status types: pure C, shared by the parser (host-tested) and the task. */

#define TUBE_MAX_LINES 6

/* Pill colours, PLAN.md §5.1, plus grey for closures that are planned or
 * routine (weekends, night), which would otherwise be red every Sunday. */
typedef enum {
    TUBE_GOOD,              /* green */
    TUBE_MINOR,             /* amber: minor delays, reduced service, ... */
    TUBE_SEVERE,            /* red: severe delays, suspended, part closure */
    TUBE_CLOSED,            /* grey: planned closure, service closed */
    TUBE_UNKNOWN,           /* grey */
} tube_severity_t;

typedef struct {
    char id[24];            /* "waterloo-city" */
    char name[24];          /* "Waterloo & City" */
    tube_severity_t severity;
    char status[32];        /* "Good Service", "Minor Delays", ... */
    char reason[320];       /* ASCII-folded; "" for good service */
} tube_line_t;

typedef struct {
    int count;
    tube_line_t line[TUBE_MAX_LINES];
    bool demo;
} tube_status_t;
