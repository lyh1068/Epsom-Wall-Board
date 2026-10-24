#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * Parser for adsbdb /v0/callsign/{callsign}. Pure C + cJSON, host-tested
 * against test/fixtures/adsbdb_callsign_*.json.
 */

typedef struct {
    char callsign[9];
    char airline_icao[4];
    char airline_name[28];
    char origin_iata[4];
    char origin_icao[5];
    char origin_city[24];       /* ASCII-folded: "Napoli", not "Nápoli" */
    char dest_iata[4];
    char dest_icao[5];
    char dest_city[24];
} route_t;

typedef enum {
    ROUTE_PARSE_ERROR = -1,     /* malformed, or not the shape we expect */
    ROUTE_PARSE_UNKNOWN = 0,    /* adsbdb: "unknown callsign" (sent with HTTP 404) */
    ROUTE_PARSE_OK = 1,
} route_parse_result_t;

route_parse_result_t route_parse(const char *json, size_t len, route_t *out);

/*
 * True when the route should be labelled "unverified": the airline is one
 * that reuses callsign suffixes across routes (PLAN.md §4), or an end of
 * the route is missing.
 */
bool route_is_unverified(const route_t *r);
