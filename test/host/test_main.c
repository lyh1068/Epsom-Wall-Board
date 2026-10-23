/*
 * Host tests for the parts that need no hardware: geo maths and the adsb.lol
 * parser. Run with test/run_host_tests.sh.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "data/adsb_parse.h"
#include "geo/geo.h"

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

int main(int argc, char **argv)
{
    const char *fixtures = argc > 1 ? argv[1] : "test/fixtures";
    test_geo();
    test_adsb_fixture(fixtures);
    test_adsb_edge_cases();
    printf("%d checks, %d failed\n", s_run, s_failed);
    return s_failed ? 1 : 0;
}
