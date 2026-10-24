#pragma once

/* Non-secret defaults. Credentials and home position come from .env. */

/* Radar centre when HOME_LAT/HOME_LON are not set in .env: Epsom station. */
#define HOME_LAT_DEFAULT        51.3343
#define HOME_LON_DEFAULT        (-0.2690)

/* Landscape. Use LV_DISPLAY_ROTATION_270 if the board hangs the other way up. */
#define UI_ROTATION             LV_DISPLAY_ROTATION_90

/* Radar range presets in nm, and which one is selected at boot. */
#define RADAR_RANGES_NM         { 10, 20, 40 }
#define RADAR_DEFAULT_RANGE     1
/* Aircraft out to this multiple of the range show as edge arrows. */
#define RADAR_EDGE_FACTOR       1.5

/* adsb.lol: never faster than this (PLAN.md §4). */
#define ADSB_POLL_S             10
#define ADSB_BACKOFF_MAX_S      160
/* Dead-reckon no further than this past a position report. */
#define ADSB_MAX_EXTRAPOLATE_S  60
#define ADSB_MAX_AIRCRAFT       128

/* adsbdb route lookups (PLAN.md §4: on demand, cached). */
#define ROUTES_CACHE_SIZE       512
#define ROUTES_TTL_FOUND_S      (7 * 86400)
#define ROUTES_TTL_UNKNOWN_S    86400
#define ROUTES_MIN_INTERVAL_MS  2000
#define ROUTES_BACKOFF_MAX_S    300
/* Flash wear: write the cache back at most this often, and only if it changed. */
#define ROUTES_SAVE_EVERY_S     600

/* Open-Meteo (PLAN.md §4: 15 min). */
#define WEATHER_POLL_S          900
#define WEATHER_BACKOFF_MAX_S   3600
#define WEATHER_RAIN_PCT        50      /* "next rain" = first hour at or above this */
#define WEATHER_SPARK_HOURS     12

/* Trains (PLAN.md §5.1). Minutes to walk to Epsom station: anything leaving
 * sooner is dimmed, and the leave-by bar counts down to "time to leave". */
#define WALK_MIN                8
#define TRAINS_ROWS             6
#define LEAVE_BY_HORIZON_MIN    60

/* Until Phases 6 and 7 bring live data, trains and tube status are demo data,
 * badged DEMO on screen. */
#define TRAINS_DEMO             1
#define TUBE_DEMO               1

/* The "overhead now" chip: nearest airborne aircraft within this distance. */
#define OVERHEAD_NM             5.0
