#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Parser for the readsb-style JSON served by adsb.lol /v2/point (and adsb.fi,
 * airplanes.live). Pure C + cJSON, so it host-tests against
 * test/fixtures/adsb_lol_point.json.
 */

typedef struct {
    char hex[8];            /* ICAO address; "~" prefix for non-ICAO */
    char flight[9];         /* callsign, trailing spaces trimmed; "" if absent */
    char type[5];           /* "t", e.g. "A20N" */
    char reg[13];           /* "r", e.g. "G-EUYA" */
    char squawk[5];
    double lat, lon;
    float seen_pos;         /* seconds since this position was received */
    float gs;               /* knots */
    float track;            /* degrees true */
    int32_t alt_baro;       /* feet */
    int32_t baro_rate;      /* ft/min */
    bool on_ground;         /* alt_baro was the string "ground" */
    bool has_alt, has_gs, has_track, has_rate;
} adsb_aircraft_t;

typedef struct {
    int64_t now_ms;         /* server time of the snapshot, ms since epoch */
    int total;              /* aircraft in the response, before any skipping */
    int skipped;            /* entries without a position */
} adsb_meta_t;

/*
 * Parse up to max aircraft into out. Returns the number written, or -1 if the
 * JSON is malformed or has no "ac" array. Entries with no lat/lon are skipped.
 */
int adsb_parse(const char *json, size_t len, adsb_aircraft_t *out, int max, adsb_meta_t *meta);

/* True for 7500 (hijack), 7600 (radio failure) and 7700 (emergency). */
bool adsb_is_emergency_squawk(const char *squawk);
