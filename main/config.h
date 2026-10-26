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

/* Aircraft feeds: adsb.fi (1 request/s, no daily cap; personal non-commercial
 * use, credited on screen), falling back to adsb.lol, whose anonymous limit
 * is 500 requests a day: at 10 s polls that lasted ~83 minutes. Never poll
 * faster than this (PLAN.md §4). */
#define ADSB_POLL_S             10
/* After adsb.lol answers 429 (daily allowance used up), leave it alone this long. */
#define ADSB_LOL_REST_S         3600
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
/* After HTTP 429: adsbdb's rate-limit window is 60 s, so wait at least that. */
#define ROUTES_429_BACKOFF_S    60
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

/* Darwin Live Departure Board via the Rail Data Marketplace (PLAN.md §4). The
 * base URL is the product's own, from its Specification tab; the key goes in
 * an x-apikey header. */
#define RAIL_LDBWS_BASE         "https://api1.raildata.org.uk/1010-live-departure-board-dep1_2/LDBWS/api/20220120"
#define TRAINS_STATION_CRS      "EPS"
#define TRAINS_WINDOW_MIN       120
/* London-bound: the destination or a later stop is one of these (PLAN.md §4). */
#define TRAINS_LONDON_CRS       "WAT,VIC,LBG,CLJ,WIM"
#define TRAINS_POLL_S           60
#define TRAINS_POLL_COMMUTE_S   30
#define TRAINS_COMMUTE_FROM_MIN (6 * 60 + 30)   /* weekdays, local time */
#define TRAINS_COMMUTE_TO_MIN   (9 * 60 + 30)
#define TRAINS_BACKOFF_MAX_S    900

/* TfL lines for the onward journey, in display order (PLAN.md §4: 2 min). */
#define TUBE_LINES              "northern,waterloo-city,jubilee,circle"
#define TUBE_POLL_S             120
#define TUBE_BACKOFF_MAX_S      900

/* Bus arrivals at the TfL stops within 300 m of Epsom station (data/bus_stops.h,
 * from tools/make_bus_stops.py). Polled round-robin: one stop every
 * BUS_POLL_S / BUS_STOP_COUNT seconds, so each refreshes every BUS_POLL_S. */
#define BUS_POLL_S              60
#define BUS_BACKOFF_MAX_S       900
#define BUS_PER_STOP            3

/* Sky page: stars to this magnitude are always drawn, as a reference chart;
 * by day and in twilight the ones too faint to see are drawn outlined. */
#define SKY_CHART_MAG           3.0f

/* Backlight, percent: the default until changed by swiping down, and the floor
 * (below this the panel is too dim to find the control again). */
#define BRIGHTNESS_DEFAULT      60
#define BRIGHTNESS_MIN          5

/* AXI read priority of the display DMA, 0..15 (everything else stays at 0). */
#define DISPLAY_DMA_QOS         15

/* 2D-DMA bandwidth cap, on each of its read and write channels: at most one
 * transfer per 2^(level+1) bus cycles on average, a 32-deep bucket allowing
 * bursts at the peak level. At 1/256 of a ~180 MHz bus with 128-byte bursts,
 * ~90 MB/s each way: a full-screen redraw still takes well under 0.1 s more,
 * and the display keeps the ~125 MB/s it reads while scanning out. */
#define DMA2D_QOS_BURST         32
#define DMA2D_QOS_PEAK_LEVEL    5
#define DMA2D_QOS_RATE_LEVEL    7

/* Sky page: redraw the chart this often. Each redraw refreshes the whole
 * 704 px chart, which tears briefly on this panel; the sky moves ~5 px in 5 min. */
#define SKY_REDRAW_MIN          5

/* The "overhead now" chip: nearest airborne aircraft within this distance. */
#define OVERHEAD_NM             5.0
