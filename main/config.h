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
