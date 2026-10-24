#include "route_text.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "data/routes.h"
#include "geo/runways.h"
#include "lvgl.h"

/* Airports drawn on the scope count as "local" for arrival/departure wording. */
static bool is_local(const char *iata)
{
    for (size_t i = 0; iata[0] && i < RUNWAY_COUNT; i++) {
        if (strcmp(RUNWAYS[i].label, iata) == 0) {
            return true;
        }
    }
    return false;
}

static const char *code(const char *iata, const char *icao)
{
    return iata[0] ? iata : (icao[0] ? icao : "?");
}

void route_short(const char *flight, char *buf, size_t size)
{
    route_t r;
    switch (routes_lookup(flight, &r)) {
    case ROUTE_ST_FOUND: {
        const char *tag = route_is_unverified(&r) ? " (unverified)" : "";
        if (is_local(r.dest_iata)) {
            snprintf(buf, size, "%s arrival%s", r.dest_iata, tag);
        } else if (is_local(r.origin_iata)) {
            snprintf(buf, size, "%s departure%s", r.origin_iata, tag);
        } else {
            snprintf(buf, size, "%s " LV_SYMBOL_RIGHT " %s%s", code(r.origin_iata, r.origin_icao),
                     code(r.dest_iata, r.dest_icao), tag);
        }
        break;
    }
    case ROUTE_ST_PENDING:
        snprintf(buf, size, "route...");
        break;
    default:
        snprintf(buf, size, "route unknown");
        break;
    }
}

void route_long(const char *flight, char *codes, char *names, size_t size)
{
    route_t r;
    switch (routes_lookup(flight, &r)) {
    case ROUTE_ST_FOUND:
        snprintf(codes, size, "%s " LV_SYMBOL_RIGHT " %s%s", code(r.origin_iata, r.origin_icao),
                 code(r.dest_iata, r.dest_icao), route_is_unverified(&r) ? "  (unverified)" : "");
        snprintf(names, size, "%s to %s%s%s", r.origin_city[0] ? r.origin_city : "?",
                 r.dest_city[0] ? r.dest_city : "?", r.airline_name[0] ? "  " "\xE2\x80\xA2" "  " : "", r.airline_name);
        break;
    case ROUTE_ST_PENDING:
        snprintf(codes, size, "Route: looking up...");
        names[0] = '\0';
        break;
    case ROUTE_ST_UNKNOWN:
        snprintf(codes, size, "Route unknown");
        snprintf(names, size, "adsbdb has no route for this callsign");
        break;
    case ROUTE_ST_ERROR:
        snprintf(codes, size, "Route lookup failed");
        snprintf(names, size, "will retry");
        break;
    case ROUTE_ST_NO_CALLSIGN:
        snprintf(codes, size, "No callsign to look up");
        names[0] = '\0';
        break;
    }
}
