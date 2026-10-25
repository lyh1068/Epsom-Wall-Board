/*
 * Host tests for the parts that need no hardware: geo maths and the adsb.lol
 * parser. Run with test/run_host_tests.sh.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/adsb_parse.h"
#include "data/bus_parse.h"
#include "data/route_cache.h"
#include "data/route_parse.h"
#include "data/text_fold.h"
#include "data/trains_logic.h"
#include "data/tube_parse.h"
#include "data/weather_parse.h"
#include "geo/geo.h"
#include "sky/astro.h"

static int s_failed, s_run;

#define CHECK(cond) do { s_run++; if (!(cond)) { s_failed++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_NEAR(a, b, tol) do { s_run++; double _a = (a), _b = (b); \
    if (fabs(_a - _b) > (tol)) { s_failed++; \
    printf("FAIL %s:%d: %s = %f, want %f +/- %f\n", __FILE__, __LINE__, #a, _a, _b, (double)(tol)); } } while (0)

#define EPSOM_LAT 51.3343
#define EPSOM_LON (-0.2690)

static void test_geo(void)
{
    geo_origin_t o;
    geo_origin_init(&o, EPSOM_LAT, EPSOM_LON);

    double x, y;
    geo_project(&o, EPSOM_LAT, EPSOM_LON, &x, &y);
    CHECK_NEAR(x, 0, 1e-9);
    CHECK_NEAR(y, 0, 1e-9);

    /* One degree of latitude is 60 nm by definition of the nautical mile. */
    geo_project(&o, EPSOM_LAT + 1.0, EPSOM_LON, &x, &y);
    CHECK_NEAR(y, 60.0, 1e-9);
    CHECK_NEAR(geo_distance_nm(0, 0, 1, 0), 60.0, 0.05);

    /* Projection agrees with the great circle within 0.5% out to ~40 nm:
     * Heathrow 27R threshold and a point 40 nm east. */
    const double pts[][2] = { { 51.477681, -0.433227 }, { EPSOM_LAT, EPSOM_LON + 40.0 / (60.0 * o.cos_lat) } };
    for (int i = 0; i < 2; i++) {
        geo_project(&o, pts[i][0], pts[i][1], &x, &y);
        double flat = hypot(x, y);
        double gc = geo_distance_nm(EPSOM_LAT, EPSOM_LON, pts[i][0], pts[i][1]);
        CHECK_NEAR(flat / gc, 1.0, 0.005);
    }

    /* Heathrow is about 10 nm north-west of Epsom. */
    double d = geo_distance_nm(EPSOM_LAT, EPSOM_LON, 51.4700, -0.4543);
    CHECK(d > 9.0 && d < 11.5);
    CHECK(strcmp(geo_compass16(geo_bearing_deg(EPSOM_LAT, EPSOM_LON, 51.4700, -0.4543)), "NW") == 0);

    CHECK_NEAR(geo_bearing_deg(0, 0, 1, 0), 0, 1e-6);
    CHECK_NEAR(geo_bearing_deg(0, 0, 0, 1), 90, 1e-6);
    CHECK_NEAR(geo_bearing_deg(0, 0, -1, 0), 180, 1e-6);
    CHECK_NEAR(geo_bearing_deg(0, 0, 0, -1), 270, 1e-6);

    /* 360 kt for 10 s is exactly 1 nm. */
    x = y = 0;
    geo_dead_reckon(&x, &y, 360, 90, 10);
    CHECK_NEAR(x, 1.0, 1e-9);
    CHECK_NEAR(y, 0.0, 1e-9);
    x = y = 0;
    geo_dead_reckon(&x, &y, 360, 180, 10);
    CHECK_NEAR(y, -1.0, 1e-9);

    CHECK(strcmp(geo_compass16(0), "N") == 0);
    CHECK(strcmp(geo_compass16(11.2), "N") == 0);
    CHECK(strcmp(geo_compass16(11.3), "NNE") == 0);
    CHECK(strcmp(geo_compass16(180), "S") == 0);
    CHECK(strcmp(geo_compass16(359), "N") == 0);
    CHECK(strcmp(geo_compass16(-90), "W") == 0);
}

static char *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    *len = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(*len);
    if (fread(buf, 1, *len, f) != *len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    return buf;
}

static const adsb_aircraft_t *find(const adsb_aircraft_t *ac, int n, const char *hex)
{
    for (int i = 0; i < n; i++) {
        if (strcmp(ac[i].hex, hex) == 0) {
            return &ac[i];
        }
    }
    return NULL;
}

static void test_adsb_fixture(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/adsb_lol_point.json", dir);
    size_t len;
    char *json = read_file(path, &len);
    CHECK(json != NULL);
    if (json == NULL) {
        return;
    }

    static adsb_aircraft_t ac[128];
    adsb_meta_t meta;
    int n = adsb_parse(json, len, ac, 128, &meta);
    CHECK(n == 67);
    CHECK(meta.total == 67);
    CHECK(meta.skipped == 0);
    CHECK(meta.now_ms == 1792785112501LL);

    const adsb_aircraft_t *a = find(ac, n, "3475d5");
    CHECK(a != NULL);
    if (a) {
        CHECK(strcmp(a->flight, "IBE07HA") == 0);     /* trailing space trimmed */
        CHECK(strcmp(a->type, "A20N") == 0);
        CHECK(strcmp(a->reg, "EC-OCS") == 0);
        CHECK(strcmp(a->squawk, "7705") == 0);
        CHECK(a->has_alt && a->alt_baro == 10675 && !a->on_ground);
        CHECK(a->has_track);
        CHECK_NEAR(a->track, 212.9, 0.01);
        CHECK_NEAR(a->gs, 300.1, 0.01);
        CHECK(a->has_rate && a->baro_rate == 2688);
        CHECK_NEAR(a->lat, 51.417618, 1e-6);
        CHECK_NEAR(a->lon, -0.858864, 1e-6);
        CHECK(!adsb_is_emergency_squawk(a->squawk));
    }

    /* Police helicopter: airborne, but no speed or track to dead-reckon with. */
    a = find(ac, n, "404714");
    CHECK(a != NULL);
    if (a) {
        CHECK(strcmp(a->flight, "GPOLH") == 0);
        CHECK(a->has_alt && a->alt_baro == 1500);
        CHECK(!a->has_gs && !a->has_track);
    }

    int ground = 0, no_flight = 0;
    for (int i = 0; i < n; i++) {
        ground += ac[i].on_ground;
        no_flight += ac[i].flight[0] == '\0';
        CHECK(ac[i].on_ground != ac[i].has_alt);   /* alt_baro is always one or the other here */
    }
    CHECK(ground == 39);
    CHECK(no_flight == 5);

    /* Truncation keeps the first max entries and still counts them all. */
    n = adsb_parse(json, len, ac, 10, &meta);
    CHECK(n == 10);
    CHECK(meta.total == 67);
    free(json);
}

/* adsb.fi v3 /lat/lon/dist: the primary feed. Same readsb shape as adsb.lol. */
static void test_adsb_fi_fixture(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/adsb_fi_v3_point.json", dir);
    size_t len;
    char *json = read_file(path, &len);
    CHECK(json != NULL);
    if (json == NULL) {
        return;
    }
    static adsb_aircraft_t ac[128];
    adsb_meta_t meta;
    int n = adsb_parse(json, len, ac, 128, &meta);
    free(json);
    CHECK(n == 78 && meta.total == 78 && meta.skipped == 0);
    CHECK(meta.now_ms == 1792928312001LL);
    int ground = 0, tracked = 0;
    for (int i = 0; i < n; i++) {
        ground += ac[i].on_ground;
        tracked += ac[i].has_track;
        CHECK(ac[i].hex[0] != '\0');
    }
    CHECK(ground == 46);
    CHECK(tracked == 41);
}

static void test_adsb_edge_cases(void)
{
    static adsb_aircraft_t ac[8];
    adsb_meta_t meta;

    const char *bad = "{\"ac\":[";
    CHECK(adsb_parse(bad, strlen(bad), ac, 8, &meta) == -1);

    const char *no_ac = "{\"msg\":\"No error\"}";
    CHECK(adsb_parse(no_ac, strlen(no_ac), ac, 8, &meta) == -1);

    const char *empty = "{\"ac\":[],\"now\":5}";
    CHECK(adsb_parse(empty, strlen(empty), ac, 8, &meta) == 0);
    CHECK(meta.now_ms == 5);

    /* No position: skipped. No hex or flight: empty strings, still kept. */
    const char *mixed = "{\"ac\":[{\"hex\":\"aaaaaa\"},{\"lat\":51.0,\"lon\":-0.1}]}";
    CHECK(adsb_parse(mixed, strlen(mixed), ac, 8, &meta) == 1);
    CHECK(meta.skipped == 1);
    CHECK(ac[0].hex[0] == '\0' && ac[0].flight[0] == '\0');
    CHECK(!ac[0].has_alt && !ac[0].on_ground);

    /* Overlong strings are cut, not overrun. */
    const char *longs = "{\"ac\":[{\"lat\":1,\"lon\":2,\"flight\":\"ABCDEFGHIJKLMNOP\",\"t\":\"TOOLONG\"}]}";
    CHECK(adsb_parse(longs, strlen(longs), ac, 8, &meta) == 1);
    CHECK(strcmp(ac[0].flight, "ABCDEFGH") == 0);
    CHECK(strcmp(ac[0].type, "TOOL") == 0);

    CHECK(adsb_is_emergency_squawk("7500"));
    CHECK(adsb_is_emergency_squawk("7600"));
    CHECK(adsb_is_emergency_squawk("7700"));
    CHECK(!adsb_is_emergency_squawk("7000"));
    CHECK(!adsb_is_emergency_squawk(""));
}

static route_parse_result_t parse_route_fixture(const char *dir, const char *name, route_t *r)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    size_t len;
    char *json = read_file(path, &len);
    CHECK(json != NULL);
    if (json == NULL) {
        return ROUTE_PARSE_ERROR;
    }
    route_parse_result_t res = route_parse(json, len, r);
    free(json);
    return res;
}

static void test_routes(const char *dir)
{
    route_t r;
    CHECK(parse_route_fixture(dir, "adsbdb_callsign_BAW371.json", &r) == ROUTE_PARSE_OK);
    CHECK(strcmp(r.callsign, "BAW371") == 0);
    CHECK(strcmp(r.airline_icao, "BAW") == 0);
    CHECK(strcmp(r.airline_name, "British Airways") == 0);
    CHECK(strcmp(r.origin_iata, "MRS") == 0 && strcmp(r.origin_icao, "LFML") == 0);
    CHECK(strcmp(r.origin_city, "Marseille") == 0);
    CHECK(strcmp(r.dest_iata, "LHR") == 0 && strcmp(r.dest_icao, "EGLL") == 0);
    CHECK(strcmp(r.dest_city, "London") == 0);
    CHECK(!route_is_unverified(&r));

    /* easyJet: route present but flagged; "Nápoli" folded to ASCII. */
    CHECK(parse_route_fixture(dir, "adsbdb_callsign_EZY8341.json", &r) == ROUTE_PARSE_OK);
    CHECK(strcmp(r.origin_iata, "LGW") == 0 && strcmp(r.dest_iata, "NAP") == 0);
    CHECK(strcmp(r.dest_city, "Napoli") == 0);
    CHECK(route_is_unverified(&r));

    CHECK(parse_route_fixture(dir, "adsbdb_callsign_unknown.json", &r) == ROUTE_PARSE_UNKNOWN);

    const char *bad = "{\"response\":{}}";
    CHECK(route_parse(bad, strlen(bad), &r) == ROUTE_PARSE_ERROR);
    CHECK(route_parse("nope", 4, &r) == ROUTE_PARSE_ERROR);

    /* Missing destination: unverified even for a reliable airline. */
    const char *half = "{\"response\":{\"flightroute\":{\"callsign\":\"BAW1\",\"airline\":{\"icao\":\"BAW\"},"
                       "\"origin\":{\"iata_code\":\"LHR\"},\"destination\":null}}}";
    CHECK(route_parse(half, strlen(half), &r) == ROUTE_PARSE_OK);
    CHECK(r.dest_iata[0] == '\0' && route_is_unverified(&r));

    /* Callsign prefix alone is enough (airline field missing). */
    const char *ryr = "{\"response\":{\"flightroute\":{\"callsign\":\"RYR3TC\","
                      "\"origin\":{\"iata_code\":\"BRS\"},\"destination\":{\"iata_code\":\"LIG\"}}}}";
    CHECK(route_parse(ryr, strlen(ryr), &r) == ROUTE_PARSE_OK);
    CHECK(route_is_unverified(&r));

    char buf[16];
    text_fold_ascii(buf, sizeof(buf), "Z\xC3\xBCrich");          /* Zürich */
    CHECK(strcmp(buf, "Zurich") == 0);
    text_fold_ascii(buf, sizeof(buf), "\xC3\x85lesund");          /* Ålesund */
    CHECK(strcmp(buf, "Alesund") == 0);
    text_fold_ascii(buf, sizeof(buf), "\xE5\x8C\x97\xE4\xBA\xAC");  /* 北京: two '?' */
    CHECK(strcmp(buf, "??") == 0);
    text_fold_ascii(buf, sizeof(buf), "bad\xC3");                  /* truncated sequence */
    CHECK(strcmp(buf, "bad?") == 0);
    text_fold_ascii(buf, 4, "Marseille");                          /* truncates, terminates */
    CHECK(strcmp(buf, "Mar") == 0);
}

#define NOW   1792785112u   /* 2026-10-23 */
#define DAY   86400u

static void test_route_cache(void)
{
    static rc_entry_t store[4];
    route_cache_t c;
    rc_init(&c, store, 4);
    route_t r = { 0 };
    strcpy(r.dest_iata, "LHR");

    CHECK(rc_get(&c, "BAW1", NOW, 7 * DAY, DAY) == NULL);
    rc_put(&c, "BAW1", RC_FOUND, &r, NOW);
    rc_put(&c, "XYZ9", RC_UNKNOWN, NULL, NOW);
    const rc_entry_t *e = rc_get(&c, "BAW1", NOW + 60, 7 * DAY, DAY);
    CHECK(e != NULL && e->kind == RC_FOUND && strcmp(e->route.dest_iata, "LHR") == 0);
    e = rc_get(&c, "XYZ9", NOW + 60, 7 * DAY, DAY);
    CHECK(e != NULL && e->kind == RC_UNKNOWN);

    /* TTLs: unknown expires after a day, found after a week. */
    CHECK(rc_get(&c, "XYZ9", NOW + 2 * DAY, 7 * DAY, DAY) == NULL);
    CHECK(rc_get(&c, "BAW1", NOW + 2 * DAY, 7 * DAY, DAY) != NULL);
    CHECK(rc_get(&c, "BAW1", NOW + 8 * DAY, 7 * DAY, DAY) == NULL);
    /* Clock not set yet: nothing expires. */
    CHECK(rc_get(&c, "BAW1", 1000, 7 * DAY, DAY) != NULL);

    /* LRU: fill to 4, touch the oldest, the next insert evicts the true LRU. */
    rc_init(&c, store, 4);
    rc_put(&c, "A", RC_UNKNOWN, NULL, NOW);
    rc_put(&c, "B", RC_UNKNOWN, NULL, NOW);
    rc_put(&c, "C", RC_UNKNOWN, NULL, NOW);
    rc_put(&c, "D", RC_UNKNOWN, NULL, NOW);
    CHECK(rc_get(&c, "A", NOW, DAY, DAY) != NULL);
    rc_put(&c, "E", RC_UNKNOWN, NULL, NOW);
    CHECK(rc_count(&c) == 4);
    CHECK(rc_get(&c, "B", NOW, DAY, DAY) == NULL);
    CHECK(rc_get(&c, "A", NOW, DAY, DAY) != NULL);
    CHECK(rc_get(&c, "E", NOW, DAY, DAY) != NULL);

    /* Round trip, and rejection of foreign or truncated data. */
    static uint8_t buf[1024];
    CHECK(c.dirty);
    size_t n = rc_serialize(&c, buf, sizeof(buf));
    CHECK(n == rc_serialized_size(&c) && n > 0);
    CHECK(!c.dirty);
    CHECK(rc_serialize(&c, buf, 8) == 0);

    static rc_entry_t store2[4];
    route_cache_t c2;
    rc_init(&c2, store2, 4);
    CHECK(rc_deserialize(&c2, buf, n));
    CHECK(rc_count(&c2) == 4);
    CHECK(rc_get(&c2, "A", NOW, DAY, DAY) != NULL);
    CHECK(rc_get(&c2, "B", NOW, DAY, DAY) == NULL);
    /* The LRU clock carries over, so new uses still sort after loaded ones. */
    CHECK(c2.clock >= c.clock);

    CHECK(!rc_deserialize(&c2, buf, n - 1));
    CHECK(rc_count(&c2) == 0);
    buf[0] ^= 0xFF;
    CHECK(!rc_deserialize(&c2, buf, n));
    CHECK(!rc_deserialize(&c2, buf, 3));
}

static void test_weather(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/open_meteo_forecast.json", dir);
    size_t len;
    char *json = read_file(path, &len);
    CHECK(json != NULL);
    if (json == NULL) {
        return;
    }
    weather_t w;
    CHECK(weather_parse(json, len, &w));
    free(json);

    CHECK(w.time == 1792789200);
    CHECK_NEAR(w.temp_c, 8.3, 1e-4);
    CHECK(w.code == 1 && !w.is_day);
    CHECK(w.cloud == -1);    /* this saved response predates asking for cloud_cover */
    CHECK(strcmp(weather_code_text(w.code), "Mainly clear") == 0);
    CHECK(w.hours == 24);
    CHECK(w.h_time[0] == 1792789200 && w.h_time[23] == 1792789200 + 23 * 3600);
    CHECK_NEAR(w.h_temp[9], 6.5, 1e-4);

    /* First hour at >= 50% is index 14, 11:00 UTC on the 24th (76%, drizzle). */
    CHECK(weather_hour_index(&w, w.time + 1800) == 0);
    CHECK(weather_next_rain(&w, w.time, 50, 24) == 14);
    CHECK(w.h_pop[14] == 76 && w.h_code[14] == 51);
    CHECK(weather_next_rain(&w, w.time, 50, 12) == -1);         /* not within 12 h */
    CHECK(weather_next_rain(&w, w.h_time[14] + 59, 50, 1) == 14);  /* raining now */
    CHECK(weather_next_rain(&w, w.time - 7200, 50, 24) == -1);  /* before the forecast */
    CHECK(weather_hour_index(&w, w.h_time[23] + 3600) == -1);   /* after it */

    /* null probabilities read as unknown, never as rain. */
    const char *nulls = "{\"current\":{\"time\":0,\"temperature_2m\":1.5,\"weather_code\":61,\"cloud_cover\":87},"
                        "\"hourly\":{\"time\":[0,3600],\"temperature_2m\":[1,2],"
                        "\"precipitation_probability\":[null,90],\"weather_code\":[null,61]}}";
    CHECK(weather_parse(nulls, strlen(nulls), &w));
    CHECK(w.hours == 2 && w.h_pop[0] == -1 && w.h_code[0] == -1);
    CHECK(w.cloud == 87);
    CHECK(w.is_day);    /* missing is_day defaults to day */
    CHECK(weather_next_rain(&w, 0, 50, 24) == 1);

    /* A null temperature ends the usable forecast there. */
    const char *gap = "{\"current\":{\"time\":0,\"temperature_2m\":1,\"weather_code\":0},"
                      "\"hourly\":{\"time\":[0,3600,7200],\"temperature_2m\":[1,null,3],"
                      "\"precipitation_probability\":[0,0,0],\"weather_code\":[0,0,0]}}";
    CHECK(weather_parse(gap, strlen(gap), &w));
    CHECK(w.hours == 1);

    const char *no_hourly = "{\"current\":{\"time\":0,\"temperature_2m\":1,\"weather_code\":0}}";
    CHECK(!weather_parse(no_hourly, strlen(no_hourly), &w));
    CHECK(!weather_parse("[]", 2, &w));
    CHECK(strcmp(weather_code_text(1234), "Unknown") == 0);
}

static train_t mk(const char *std, const char *etd, bool london)
{
    train_t t = { 0 };
    strcpy(t.std, std);
    strcpy(t.etd, etd);
    t.london = london;
    return t;
}

static void test_trains_logic(void)
{
    CHECK(hhmm_to_min("07:48") == 468);
    CHECK(hhmm_to_min("00:00") == 0);
    CHECK(hhmm_to_min("23:59") == 1439);
    CHECK(hhmm_to_min("24:00") == -1);
    CHECK(hhmm_to_min("7:48") == -1);
    CHECK(hhmm_to_min("On time") == -1);
    CHECK(hhmm_to_min("") == -1);

    /* Midnight wrap both ways. */
    CHECK(minutes_until(23 * 60 + 50, 10) == 20);
    CHECK(minutes_until(2, 23 * 60 + 58) == -4);
    CHECK(minutes_until(600, 615) == 15);

    int exp;
    train_t t = mk("07:55", "07:58", true);
    CHECK(train_etd(&t, &exp) == ETD_EXPECTED && exp == 478);
    CHECK(train_delay_min(&t) == 3);
    t = mk("07:48", "On time", true);
    CHECK(train_etd(&t, &exp) == ETD_ON_TIME && exp == 468);
    CHECK(train_delay_min(&t) == 0);
    t = mk("08:02", "Cancelled", true);
    CHECK(train_etd(&t, &exp) == ETD_CANCELLED && exp == -1);
    t = mk("08:10", "Delayed", true);
    CHECK(train_etd(&t, &exp) == ETD_DELAYED && exp == 490);
    t = mk("23:55", "00:03", true);             /* delay across midnight */
    CHECK(train_delay_min(&t) == 8);

    /* The plan's example board at 07:42 with an 8-minute walk. */
    train_t board[] = {
        mk("07:48", "On time", true),           /* 6 min away: can't make it */
        mk("07:55", "07:58", true),             /* 16 min: the one */
        mk("08:02", "Cancelled", true),
        mk("08:10", "On time", true),
    };
    leave_by_t lb;
    CHECK(leave_by(board, 4, 7 * 60 + 42, 8, 60, &lb));
    CHECK(lb.index == 1 && lb.leave_in == 8);

    /* Later: the 07:58 is 9 min away -> leave in 1 ("go now" band). */
    CHECK(leave_by(board, 4, 7 * 60 + 49, 8, 60, &lb));
    CHECK(lb.index == 1 && lb.leave_in == 1);
    /* Exactly walk_min away still counts; one minute later it does not. */
    CHECK(leave_by(board, 4, 7 * 60 + 50, 8, 60, &lb) && lb.index == 1 && lb.leave_in == 0);
    CHECK(leave_by(board, 4, 7 * 60 + 51, 8, 60, &lb) && lb.index == 3);   /* skips the cancelled 08:02 */

    /* Non-London services never count; nothing within the horizon -> false. */
    train_t only_local[] = { mk("08:00", "On time", false) };
    CHECK(!leave_by(only_local, 1, 7 * 60 + 30, 8, 60, &lb));
    train_t far[] = { mk("10:00", "On time", true) };
    CHECK(!leave_by(far, 1, 7 * 60 + 30, 8, 60, &lb));
    CHECK(!leave_by(board, 0, 0, 8, 60, &lb));

    /* Across midnight: 00:10 seen at 23:55 is 15 min away. */
    train_t late[] = { mk("00:10", "On time", true) };
    CHECK(leave_by(late, 1, 23 * 60 + 55, 8, 60, &lb) && lb.leave_in == 7);
}

static const tube_line_t *line_by_id(const tube_status_t *t, const char *id)
{
    for (int i = 0; i < t->count; i++) {
        if (strcmp(t->line[i].id, id) == 0) {
            return &t->line[i];
        }
    }
    return NULL;
}

static void test_tube(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/tfl_line_status.json", dir);
    size_t len;
    char *json = read_file(path, &len);
    CHECK(json != NULL);
    if (json == NULL) {
        return;
    }

    /* Saturday 24 Oct 2026: Circle part closure, W&C closed for the weekend. */
    tube_status_t t;
    CHECK(tube_parse(json, len, "northern,waterloo-city,jubilee,circle", &t));
    CHECK(t.count == 4);
    /* Configured order, not TfL's alphabetical order. */
    CHECK(strcmp(t.line[0].id, "northern") == 0 && strcmp(t.line[1].id, "waterloo-city") == 0);
    CHECK(strcmp(t.line[2].id, "jubilee") == 0 && strcmp(t.line[3].id, "circle") == 0);

    const tube_line_t *l = line_by_id(&t, "northern");
    CHECK(l && l->severity == TUBE_GOOD && strcmp(l->status, "Good Service") == 0 && l->reason[0] == '\0');
    l = line_by_id(&t, "circle");
    CHECK(l && l->severity == TUBE_SEVERE && strcmp(l->status, "Part Closure") == 0);
    CHECK(l && strncmp(l->reason, "CIRCLE LINE:", 12) == 0);
    /* Two statuses (Planned Closure, Service Closed): both routine, so grey. */
    l = line_by_id(&t, "waterloo-city");
    CHECK(l && l->severity == TUBE_CLOSED && strcmp(l->name, "Waterloo & City") == 0);

    /* Fewer lines, and an id TfL did not return, are both fine. */
    CHECK(tube_parse(json, len, "circle,bakerloo", &t));
    CHECK(t.count == 1 && strcmp(t.line[0].id, "circle") == 0);
    free(json);

    /* Worst status wins regardless of order; minor beats a planned closure. */
    const char *two = "[{\"id\":\"x\",\"name\":\"X\",\"lineStatuses\":["
                      "{\"statusSeverity\":4,\"statusSeverityDescription\":\"Planned Closure\",\"reason\":\"a\"},"
                      "{\"statusSeverity\":9,\"statusSeverityDescription\":\"Minor Delays\",\"reason\":\"b\"}]}]";
    CHECK(tube_parse(two, strlen(two), "x", &t));
    CHECK(t.line[0].severity == TUBE_MINOR && strcmp(t.line[0].reason, "b") == 0);

    /* Typographic punctuation in reasons is folded for the fonts. */
    const char *quotes = "[{\"id\":\"y\",\"name\":\"Y\",\"lineStatuses\":[{\"statusSeverity\":6,"
                         "\"statusSeverityDescription\":\"Severe Delays\","
                         "\"reason\":\"Y line \xE2\x80\x93 a \xE2\x80\x98signal\xE2\x80\x99 fault\xE2\x80\xA6\"}]}]";
    CHECK(tube_parse(quotes, strlen(quotes), "y", &t));
    CHECK(strcmp(t.line[0].reason, "Y line - a 'signal' fault.") == 0);

    const char *nostatus = "[{\"id\":\"z\",\"name\":\"Z\",\"lineStatuses\":[]}]";
    CHECK(tube_parse(nostatus, strlen(nostatus), "z", &t));
    CHECK(t.line[0].severity == TUBE_UNKNOWN);

    CHECK(!tube_parse("{\"message\":\"Invalid app_key\"}", 30, "x", &t));
    CHECK(!tube_parse("nope", 4, "x", &t));

    CHECK(tube_classify(10) == TUBE_GOOD);
    CHECK(tube_classify(9) == TUBE_MINOR);
    CHECK(tube_classify(6) == TUBE_SEVERE && tube_classify(2) == TUBE_SEVERE && tube_classify(5) == TUBE_SEVERE);
    CHECK(tube_classify(20) == TUBE_CLOSED && tube_classify(4) == TUBE_CLOSED);
    CHECK(tube_classify(99) == TUBE_UNKNOWN);
}

static void test_bus(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/tfl_bus_arrivals.json", dir);
    size_t len;
    char *json = read_file(path, &len);
    CHECK(json != NULL);
    if (json == NULL) {
        return;
    }

    /* Epsom High Street (G), Saturday morning: 5 predictions, unsorted. */
    bus_board_t b;
    CHECK(bus_parse(json, len, &b));
    free(json);
    CHECK(b.count == 5);
    CHECK(strcmp(b.stop_name, "Epsom High Street") == 0 && strcmp(b.stop_letter, "G") == 0);
    CHECK(strcmp(b.a[0].line, "406") == 0 && strcmp(b.a[0].dest, "Kingston") == 0 && b.a[0].secs == 236);
    CHECK(strcmp(b.a[1].line, "S2") == 0 && strcmp(b.a[1].dest, "St Helier Station") == 0);
    CHECK(strcmp(b.a[2].line, "293") == 0 && b.a[2].secs == 808);
    for (int i = 1; i < b.count; i++) {
        CHECK(b.a[i - 1].secs <= b.a[i].secs);
    }

    /* More than BUS_MAX: keeps the soonest, wherever they sit in the response. */
    char big[2048] = "[";
    for (int i = 0; i < BUS_MAX + 4; i++) {
        char one[96];
        snprintf(one, sizeof(one), "%s{\"lineName\":\"L%d\",\"destinationName\":\"D\",\"timeToStation\":%d}",
                 i ? "," : "", i, 1000 - i * 60);
        strcat(big, one);
    }
    strcat(big, "]");
    CHECK(bus_parse(big, strlen(big), &b));
    CHECK(b.count == BUS_MAX);
    CHECK(b.a[0].secs == 1000 - (BUS_MAX + 3) * 60);    /* the last one in the array */

    /* Nothing due (late night): valid, empty, no stop name. */
    CHECK(bus_parse("[]", 2, &b));
    CHECK(b.count == 0 && b.stop_name[0] == '\0');

    /* TfL error object, garbage. */
    const char *err = "{\"$type\":\"Tfl.Apps.Api.Models.ApiError\",\"httpStatusCode\":404}";
    CHECK(!bus_parse(err, strlen(err), &b));
    CHECK(!bus_parse("x", 1, &b));

    char w[16];
    bus_fmt_wait(w, sizeof(w), 59);
    CHECK(strcmp(w, "due") == 0);
    bus_fmt_wait(w, sizeof(w), 60);
    CHECK(strcmp(w, "1 min") == 0);
    bus_fmt_wait(w, sizeof(w), 808);
    CHECK(strcmp(w, "13 min") == 0);
    bus_fmt_wait(w, sizeof(w), -30);                    /* overdue: still "due" */
    CHECK(strcmp(w, "due") == 0);
}

/* Angular difference in degrees, wrapping azimuth. */
static double adiff(double a, double b)
{
    double d = fmod(fabs(a - b), 360.0);
    return d > 180 ? 360 - d : d;
}

static void test_sky(void)
{
    /* Meeus example 12.a: 1987 April 10, 0h UT -> GMST 13h10m46.3668s. */
    CHECK_NEAR(astro_gmst(2446895.5), (13 + 10 / 60.0 + 46.3668 / 3600.0) * 15, 0.0001);

    /* Meeus example 21.b without proper motion: theta Persei to 2028 Nov 13.19. */
    double ra, dec;
    astro_precess(41.054063, 49.227750, 2462088.69, &ra, &dec);
    CHECK_NEAR(ra, 41.547214, 0.01);
    CHECK_NEAR(dec, 49.348483, 0.01);

    /* JPL Horizons, observer at Epsom station (51.3343 N, 0.2690 W), airless
     * apparent az/el, 2026-10-24 12:00 and 20:00 UTC. */
    const observer_t eps = { 51.3343, -0.2690 };
    const time_t t12 = 1792843200, t20 = 1792872000;
    static const struct { int body; double az12, alt12, az20, alt20, tol; } H[] = {
        { -1,             184.034378, 26.728810, 289.868961, -30.066236, 0.05 },  /* Sun */
        { PLANET_MERCURY, 165.322945, 16.579282, 268.086494, -25.574513, 0.15 },
        { PLANET_VENUS,   186.700128, 20.720670, 288.889703, -36.507577, 0.15 },
        { PLANET_MARS,    269.882469, 23.128427,  15.298886, -19.663383, 0.15 },
        { PLANET_JUPITER, 260.821163, 25.644104,   7.444434, -24.031874, 0.15 },
        { PLANET_SATURN,   27.551722, -33.897276, 135.246626, 31.148781, 0.15 },
    };
    for (size_t i = 0; i < sizeof(H) / sizeof(H[0]); i++) {
        for (int k = 0; k < 2; k++) {
            double jd = astro_jd(k ? t20 : t12), alt, az;
            if (H[i].body < 0) {
                astro_sun(jd, &ra, &dec);
            } else {
                astro_planet((planet_t)H[i].body, jd, &ra, &dec);
            }
            astro_horizontal(&eps, jd, ra, dec, &alt, &az);
            double want_az = k ? H[i].az20 : H[i].az12, want_alt = k ? H[i].alt20 : H[i].alt12;
            /* Azimuth error matters less near the zenith; this sky has nothing above 37 deg. */
            if (adiff(az, want_az) > H[i].tol || fabs(alt - want_alt) > H[i].tol) {
                s_failed++;
                printf("FAIL body %d at %s: az %.3f alt %.3f, Horizons az %.3f alt %.3f\n", H[i].body,
                       k ? "20:00" : "12:00", az, alt, want_az, want_alt);
            }
            s_run++;
        }
    }

    /* Moon: low-precision series plus parallax, so a looser 0.5 deg. */
    double alt, az, illum, phase;
    astro_moon_horizontal(&eps, astro_jd(t12), &alt, &az, &illum, &phase);
    CHECK(adiff(az, 29.009109) < 0.5 && fabs(alt - -28.801041) < 0.5);
    astro_moon_horizontal(&eps, astro_jd(t20), &alt, &az, &illum, &phase);
    CHECK(adiff(az, 129.814932) < 0.5 && fabs(alt - 36.368034) < 0.5);
    /* Two days before full moon (26 Oct 2026). */
    CHECK(illum > 0.85 && illum < 1.0 && phase < 45);

    /* Sunrise at Epsom on 24 Oct 2026: about 06:40 UTC (07:40 BST), and the
     * returned instant really is on the -0.833 deg line. */
    time_t rise = astro_sun_crossing(&eps, 1792800000 /* 00:00 UTC */, -0.833, true);
    CHECK(rise > 1792800000 + 6 * 3600 + 20 * 60 && rise < 1792800000 + 7 * 3600);
    astro_sun(astro_jd(rise), &ra, &dec);
    astro_horizontal(&eps, astro_jd(rise), ra, dec, &alt, &az);
    CHECK_NEAR(alt, -0.833, 0.01);
    CHECK(az > 90 && az < 130);         /* east-south-east in late October */
    time_t set = astro_sun_crossing(&eps, rise, -0.833, false);
    CHECK(set > 1792800000 + 16 * 3600 + 30 * 60 && set < 1792800000 + 17 * 3600 + 10 * 60);  /* ~16:49 UTC */

    CHECK(strcmp(astro_sky_state(10), "Day") == 0);
    CHECK(strcmp(astro_sky_state(-3), "Civil twilight") == 0);
    CHECK(strcmp(astro_sky_state(-9), "Nautical twilight") == 0);
    CHECK(strcmp(astro_sky_state(-15), "Astronomical twilight") == 0);
    CHECK(strcmp(astro_sky_state(-30), "Night") == 0);

    /* Polaris sits within a degree of the latitude, due north. */
    astro_precess(37.9546, 89.2641, astro_jd(t20), &ra, &dec);
    astro_horizontal(&eps, astro_jd(t20), ra, dec, &alt, &az);
    CHECK(fabs(alt - eps.lat) < 1.0 && adiff(az, 0) < 1.5);
}

int main(int argc, char **argv)
{
    const char *fixtures = argc > 1 ? argv[1] : "test/fixtures";
    test_geo();
    test_adsb_fixture(fixtures);
    test_adsb_fi_fixture(fixtures);
    test_adsb_edge_cases();
    test_routes(fixtures);
    test_route_cache();
    test_weather(fixtures);
    test_trains_logic();
    test_tube(fixtures);
    test_bus(fixtures);
    test_sky();
    printf("%d checks, %d failed\n", s_run, s_failed);
    return s_failed ? 1 : 0;
}
