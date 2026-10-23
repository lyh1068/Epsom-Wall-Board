#include "radar.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "data/aircraft.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "geo/geo.h"
#include "geo/runways.h"
#include "ui/icons.h"
#include "ui/status_corner.h"
#include "ui/ui_util.h"

static const char *TAG = "radar";

/*
 * Design (tab5-board skill, display.md): the panel is rotated in software into
 * a single live framebuffer, so redraw area is what tears. The rings, labels
 * and runways are static objects, only touched on a range change. Each
 * aircraft is a small pooled image moved only when its pixel position changes,
 * so a frame's dirty area is a handful of small rectangles, not the scope.
 */

#define SCOPE       720
#define CX          (SCOPE / 2)
#define CY          (SCOPE / 2)
#define RANGE_PX    330             /* radius of the outer ring */
#define EDGE_PX     344             /* where edge arrows sit */
#define PANEL_W     560
#define PANEL_PAD   24
#define CONTENT_W   (PANEL_W - 2 * PANEL_PAD)
#define FRAME_MS    200             /* ~5 fps dead reckoning */
#define PANEL_EVERY 3               /* panel text every 3rd frame */
#define MAX_SLOTS   64
#define TRAIL_LEN   8
#define DOT         6

#define C_RING      lv_color_hex(0x1B5E20)
#define C_RING_TXT  lv_color_hex(0x43A047)
#define C_RUNWAY    lv_color_hex(0x78909C)
#define C_TEXT      lv_color_hex(0xECEFF1)
#define C_DIM       lv_color_hex(0x90A4AE)
#define C_PANEL     lv_color_hex(0x0B0F12)
#define C_EMERG     0xFF1744

typedef enum { DRAW_HIDDEN, DRAW_PLANE, DRAW_EDGE } draw_t;

typedef struct {
    bool used;
    adsb_aircraft_t ac;
    double x0, y0;              /* nm east/north of home, at t0 */
    int64_t t0_us;              /* when that position was current */
    double x, y;                /* dead-reckoned now */
    float trail_x[TRAIL_LEN], trail_y[TRAIL_LEN];
    uint8_t trail_n, trail_head;
    lv_obj_t *icon, *label, *dots[TRAIL_LEN];
    const void *src;
    int32_t px, py, lx, ly, rot;
    uint32_t color;
    draw_t draw;
} slot_t;

static const int s_ranges[] = RADAR_RANGES_NM;
#define RANGE_COUNT ((int)(sizeof(s_ranges) / sizeof(s_ranges[0])))
static int s_range_idx = RADAR_DEFAULT_RANGE;

static radar_icons_t s_icons;
static slot_t *s_slots;
static adsb_aircraft_t *s_snap;
static uint32_t s_seq;
static int s_selected = -1;
static int s_shown;
static int s_emergency = -1;

static lv_obj_t *s_scope, *s_sel_ring, *s_banner;
static lv_obj_t *s_ring_lbl[3];
static lv_obj_t *s_rwy[RUNWAY_COUNT], *s_rwy_lbl[RUNWAY_COUNT];
static lv_point_precise_t s_rwy_pts[RUNWAY_COUNT][2];
static lv_obj_t *s_range_btn[RANGE_COUNT];
static lv_obj_t *s_detail[6];

static double range_nm(void)
{
    return s_ranges[s_range_idx];
}

static double scale(void)
{
    return RANGE_PX / range_nm();
}

static uint32_t alt_color(const adsb_aircraft_t *a)
{
    if (adsb_is_emergency_squawk(a->squawk)) {
        return C_EMERG;
    }
    if (!a->has_alt) {
        return 0x9E9E9E;
    }
    if (a->alt_baro < 3000) {
        return 0xFF6D00;
    }
    if (a->alt_baro < 10000) {
        return 0xFFD600;
    }
    if (a->alt_baro < 25000) {
        return 0x00E676;
    }
    return 0x18FFFF;
}

static lv_obj_t *plain_obj(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_clickable(o, false);
    lv_obj_set_scrollable(o, false);
    return o;
}

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, lv_color_t c, const char *s)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, s);
    return l;
}

/* ---- static scope: rings, compass, runways --------------------------------------- */

static void layout_static(void)
{
    const int r = (int)range_nm();
    char buf[16];
    for (int i = 0; i < 3; i++) {
        double nm = r / (double)(1 << (2 - i));     /* r/4, r/2, r */
        snprintf(buf, sizeof(buf), i == 2 ? "%g nm" : "%g", nm);
        lv_label_set_text(s_ring_lbl[i], buf);
    }

    const geo_origin_t *home = aircraft_home();
    const double k = scale();
    for (size_t i = 0; i < RUNWAY_COUNT; i++) {
        double x1, y1, x2, y2;
        geo_project(home, RUNWAYS[i].lat1, RUNWAYS[i].lon1, &x1, &y1);
        geo_project(home, RUNWAYS[i].lat2, RUNWAYS[i].lon2, &x2, &y2);
        bool visible = hypot((x1 + x2) / 2, (y1 + y2) / 2) <= range_nm();
        lv_obj_set_hidden(s_rwy[i], !visible);
        if (s_rwy_lbl[i]) {
            lv_obj_set_hidden(s_rwy_lbl[i], !visible);
        }
        if (!visible) {
            continue;
        }
        int32_t ax = CX + lround(x1 * k), ay = CY - lround(y1 * k);
        int32_t bx = CX + lround(x2 * k), by = CY - lround(y2 * k);
        int32_t mx = ax < bx ? ax : bx, my = ay < by ? ay : by;
        /* Points are relative to the line object, so keep them non-negative. */
        lv_obj_set_pos(s_rwy[i], mx, my);
        s_rwy_pts[i][0] = (lv_point_precise_t) { ax - mx, ay - my };
        s_rwy_pts[i][1] = (lv_point_precise_t) { bx - mx, by - my };
        lv_line_set_points(s_rwy[i], s_rwy_pts[i], 2);
        if (s_rwy_lbl[i]) {
            lv_obj_set_pos(s_rwy_lbl[i], (ax + bx) / 2 - 14, (ay + by) / 2 + 8);
        }
    }
}

static void build_static(void)
{
    static const int ring_r[3] = { RANGE_PX / 4, RANGE_PX / 2, RANGE_PX };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *ring = plain_obj(s_scope);
        int r = ring_r[i];
        lv_obj_set_size(ring, 2 * r + 1, 2 * r + 1);
        lv_obj_set_pos(ring, CX - r, CY - r);
        lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(ring, C_RING, 0);
        lv_obj_set_style_border_width(ring, i == 2 ? 2 : 1, 0);

        s_ring_lbl[i] = text(s_scope, &lv_font_montserrat_14, C_RING_TXT, "");
        lv_obj_align(s_ring_lbl[i], LV_ALIGN_CENTER, (int32_t)(r * 0.707) + 14, -(int32_t)(r * 0.707) - 8);
    }

    static const struct { const char *t; int dx, dy; } compass[] = {
        { "N", 0, -(RANGE_PX + 14) }, { "S", 0, RANGE_PX + 14 },
        { "E", RANGE_PX + 14, 0 }, { "W", -(RANGE_PX + 14), 0 },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = text(s_scope, &lv_font_montserrat_14, C_RING_TXT, compass[i].t);
        lv_obj_align(l, LV_ALIGN_CENTER, compass[i].dx, compass[i].dy);
    }

    for (size_t i = 0; i < RUNWAY_COUNT; i++) {
        s_rwy[i] = lv_line_create(s_scope);
        lv_obj_set_style_line_color(s_rwy[i], C_RUNWAY, 0);
        lv_obj_set_style_line_width(s_rwy[i], 3, 0);
        lv_obj_set_style_line_rounded(s_rwy[i], true, 0);
        /* One label per airport, on its first runway. */
        bool first = i == 0 || strcmp(RUNWAYS[i].label, RUNWAYS[i - 1].label) != 0;
        s_rwy_lbl[i] = first ? text(s_scope, &lv_font_montserrat_14, C_RUNWAY, RUNWAYS[i].label) : NULL;
    }

    lv_obj_t *home = text(s_scope, &lv_font_montserrat_20, C_TEXT, LV_SYMBOL_HOME);
    lv_obj_align(home, LV_ALIGN_CENTER, 0, 0);

    layout_static();
}

/* ---- aircraft slots ---------------------------------------------------------------- */

static void on_plane_clicked(lv_event_t *e)
{
    s_selected = (int)(intptr_t)lv_event_get_user_data(e);
}

static void on_scope_clicked(lv_event_t *e)
{
    s_selected = -1;
}

static void slot_create_objs(slot_t *s, int idx)
{
    for (int k = 0; k < TRAIL_LEN; k++) {
        lv_obj_t *d = plain_obj(s_scope);
        lv_obj_set_size(d, DOT, DOT);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, C_DIM, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_set_hidden(d, true);
        s->dots[k] = d;
    }

    s->icon = lv_image_create(s_scope);
    lv_obj_set_hidden(s->icon, true);
    lv_obj_set_clickable(s->icon, true);
    lv_obj_set_adv_hittest(s->icon, false);     /* tap the box, not just opaque pixels */
    lv_obj_set_ext_click_area(s->icon, 14);
    lv_obj_set_style_image_recolor_opa(s->icon, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s->icon, on_plane_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)idx);

    s->label = text(s_scope, &lv_font_montserrat_14, C_TEXT, "");
    lv_obj_set_hidden(s->label, true);

    /* Keep overlays above the planes created after them. */
    lv_obj_move_foreground(s_sel_ring);
    lv_obj_move_foreground(s_banner);
}

static void slot_free(slot_t *s)
{
    s->used = false;
    s->draw = DRAW_HIDDEN;
    if (s->icon) {
        ui_set_hidden(s->icon, true);
        ui_set_hidden(s->label, true);
        for (int k = 0; k < TRAIL_LEN; k++) {
            ui_set_hidden(s->dots[k], true);
        }
    }
}

static void layout_trail(slot_t *s)
{
    const double k = scale();
    for (int i = 0; i < TRAIL_LEN; i++) {
        lv_obj_t *d = s->dots[i];
        if (i >= s->trail_n) {
            ui_set_hidden(d, true);
            continue;
        }
        int idx = (s->trail_head - i + TRAIL_LEN) % TRAIL_LEN;
        double x = s->trail_x[idx], y = s->trail_y[idx];
        if (hypot(x, y) > range_nm()) {
            ui_set_hidden(d, true);
            continue;
        }
        lv_obj_set_pos(d, CX + lround(x * k) - DOT / 2, CY - lround(y * k) - DOT / 2);
        lv_obj_set_style_opa(d, (lv_opa_t)(LV_OPA_60 - i * 7), 0);
        ui_set_hidden(d, false);
    }
}

static void trail_push(slot_t *s, double x, double y)
{
    s->trail_head = (s->trail_head + 1) % TRAIL_LEN;
    s->trail_x[s->trail_head] = (float)x;
    s->trail_y[s->trail_head] = (float)y;
    if (s->trail_n < TRAIL_LEN) {
        s->trail_n++;
    }
}

static int slot_find(const char *hex)
{
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].used && strcmp(s_slots[i].ac.hex, hex) == 0) {
            return i;
        }
    }
    return -1;
}

static int slot_alloc(void)
{
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (!s_slots[i].used) {
            slot_t *s = &s_slots[i];
            if (s->icon == NULL) {
                slot_create_objs(s, i);
            }
            s->used = true;
            s->trail_n = 0;
            s->src = NULL;
            s->px = s->py = s->lx = s->ly = INT32_MIN;
            s->rot = INT32_MIN;
            s->color = 0xFFFFFFFF;
            s->draw = DRAW_HIDDEN;
            return i;
        }
    }
    return -1;
}

/* A new poll arrived: match aircraft to slots by hex, free the ones that left. */
static void ingest(int n, const aircraft_meta_t *m)
{
    static bool seen[MAX_SLOTS];
    memset(seen, 0, sizeof(seen));
    const geo_origin_t *home = aircraft_home();
    const double max_nm = range_nm() * RADAR_EDGE_FACTOR;

    for (int i = 0; i < n; i++) {
        const adsb_aircraft_t *a = &s_snap[i];
        if (a->on_ground || a->hex[0] == '\0') {
            continue;
        }
        double x, y;
        geo_project(home, a->lat, a->lon, &x, &y);
        if (hypot(x, y) > max_nm) {
            continue;
        }
        int idx = slot_find(a->hex);
        if (idx < 0) {
            idx = slot_alloc();
            if (idx < 0) {
                continue;   /* pool full: farthest-first culling is a later refinement */
            }
        } else if (hypot(x - s_slots[idx].x0, y - s_slots[idx].y0) > 0.05) {
            trail_push(&s_slots[idx], s_slots[idx].x0, s_slots[idx].y0);
        }
        slot_t *s = &s_slots[idx];
        bool relabel = strcmp(s->ac.flight, a->flight) != 0 || s->src == NULL;
        s->ac = *a;
        s->x0 = x;
        s->y0 = y;
        s->t0_us = m->fetched_us - (int64_t)(a->seen_pos * 1e6f);
        seen[idx] = true;
        if (relabel) {
            lv_label_set_text(s->label, a->flight[0] ? a->flight : a->hex);
        }
        layout_trail(s);
    }

    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].used && !seen[i]) {
            if (s_selected == i) {
                s_selected = -1;
            }
            slot_free(&s_slots[i]);
        }
    }
}

static void apply(slot_t *s, draw_t draw, int32_t cx, int32_t cy, const lv_image_dsc_t *src, int32_t rot)
{
    s->draw = draw;
    if (draw == DRAW_HIDDEN) {
        ui_set_hidden(s->icon, true);
        ui_set_hidden(s->label, true);
        return;
    }
    if (s->src != src) {
        lv_image_set_src(s->icon, src);
        s->src = src;
        s->rot = INT32_MIN;
    }
    int32_t px = cx - ICON_SIZE / 2, py = cy - ICON_SIZE / 2;
    if (px != s->px || py != s->py) {
        lv_obj_set_pos(s->icon, px, py);
        s->px = px;
        s->py = py;
    }
    if (s->rot == INT32_MIN || labs(rot - s->rot) >= 10) {    /* 1 degree */
        lv_image_set_rotation(s->icon, rot);
        s->rot = rot;
    }
    uint32_t c = alt_color(&s->ac);
    if (c != s->color) {
        lv_obj_set_style_image_recolor(s->icon, lv_color_hex(c), 0);
        s->color = c;
    }
    ui_set_hidden(s->icon, false);

    if (draw == DRAW_EDGE) {
        ui_set_hidden(s->label, true);
        return;
    }
    int32_t lx = px + ICON_SIZE - 2, ly = py + 8;
    if (lx != s->lx || ly != s->ly) {
        lv_obj_set_pos(s->label, lx, ly);
        s->lx = lx;
        s->ly = ly;
    }
    ui_set_hidden(s->label, false);
}

static void render(void)
{
    const int64_t now = esp_timer_get_time();
    const double r = range_nm(), k = scale();
    int shown = 0, emergency = -1;

    for (int i = 0; i < MAX_SLOTS; i++) {
        slot_t *s = &s_slots[i];
        if (!s->used) {
            continue;
        }
        const adsb_aircraft_t *a = &s->ac;
        double dt = (now - s->t0_us) / 1e6;
        dt = dt < 0 ? 0 : (dt > ADSB_MAX_EXTRAPOLATE_S ? ADSB_MAX_EXTRAPOLATE_S : dt);
        double x = s->x0, y = s->y0;
        if (a->has_gs && a->has_track) {
            geo_dead_reckon(&x, &y, a->gs, a->track, dt);
        }
        s->x = x;
        s->y = y;

        double d = hypot(x, y);
        if (d <= r) {
            apply(s, DRAW_PLANE, CX + lround(x * k), CY - lround(y * k),
                  a->has_track ? s_icons.plane : s_icons.blip,
                  a->has_track ? lround(a->track * 10) : 0);
            shown++;
        } else if (d <= r * RADAR_EDGE_FACTOR) {
            double b = atan2(x, y);     /* bearing from home, radians, 0 = north */
            int32_t rot = lround(b * 1800.0 / M_PI);
            apply(s, DRAW_EDGE, CX + lround(EDGE_PX * sin(b)), CY - lround(EDGE_PX * cos(b)),
                  s_icons.arrow, (rot + 3600) % 3600);
        } else {
            apply(s, DRAW_HIDDEN, 0, 0, NULL, 0);
        }
        if (s->draw != DRAW_HIDDEN && adsb_is_emergency_squawk(a->squawk)) {
            emergency = i;
        }
    }
    s_shown = shown;
    s_emergency = emergency;

    if (s_selected >= 0 && s_slots[s_selected].draw == DRAW_PLANE) {
        const slot_t *s = &s_slots[s_selected];
        if (lv_obj_get_x(s_sel_ring) != s->px - 6 || lv_obj_get_y(s_sel_ring) != s->py - 6) {
            lv_obj_set_pos(s_sel_ring, s->px - 6, s->py - 6);
        }
        ui_set_hidden(s_sel_ring, false);
    } else {
        ui_set_hidden(s_sel_ring, true);
    }
}

/* ---- right-hand panel --------------------------------------------------------------- */

static void fmt_thousands(char *buf, size_t size, int v)
{
    if (v >= 1000) {
        snprintf(buf, size, "%d,%03d", v / 1000, v % 1000);
    } else {
        snprintf(buf, size, "%d", v);
    }
}

static const char *squawk_meaning(const char *sq)
{
    if (strcmp(sq, "7500") == 0) {
        return "HIJACK";
    }
    if (strcmp(sq, "7600") == 0) {
        return "RADIO FAILURE";
    }
    return "EMERGENCY";
}

static void refresh_panel(void)
{
    char l[6][96];
    if (s_selected < 0) {
        snprintf(l[0], sizeof(l[0]), "%d aircraft", s_shown);
        snprintf(l[1], sizeof(l[1]), "within %d nm of home", (int)range_nm());
        snprintf(l[2], sizeof(l[2]), "Tap a plane for details");
        l[3][0] = l[4][0] = l[5][0] = '\0';
    } else {
        const slot_t *s = &s_slots[s_selected];
        const adsb_aircraft_t *a = &s->ac;
        snprintf(l[0], sizeof(l[0]), "%s", a->flight[0] ? a->flight : a->hex);
        snprintf(l[1], sizeof(l[1]), "%s  %s", a->type[0] ? a->type : "type ?", a->reg);

        char alt[16];
        if (a->has_alt) {
            fmt_thousands(alt, sizeof(alt), a->alt_baro);
            if (a->has_rate && abs(a->baro_rate) >= 200) {
                snprintf(l[2], sizeof(l[2]), "%s ft  %s %d fpm", alt,
                         a->baro_rate > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, abs(a->baro_rate));
            } else {
                snprintf(l[2], sizeof(l[2]), "%s ft  level", alt);
            }
        } else {
            snprintf(l[2], sizeof(l[2]), "altitude unknown");
        }

        if (a->has_gs && a->has_track) {
            snprintf(l[3], sizeof(l[3]), "%d kt  track %03d\xC2\xB0", (int)lroundf(a->gs), (int)lroundf(a->track) % 360);
        } else if (a->has_gs) {
            snprintf(l[3], sizeof(l[3]), "%d kt", (int)lroundf(a->gs));
        } else {
            snprintf(l[3], sizeof(l[3]), "no speed or track reported");
        }

        double d = hypot(s->x, s->y);
        double b = fmod(atan2(s->x, s->y) * 180.0 / M_PI + 360.0, 360.0);
        snprintf(l[4], sizeof(l[4]), "%.1f nm %s of home", d, geo_compass16(b));

        if (adsb_is_emergency_squawk(a->squawk)) {
            snprintf(l[5], sizeof(l[5]), "Squawk %s  %s", a->squawk, squawk_meaning(a->squawk));
        } else {
            snprintf(l[5], sizeof(l[5]), "Squawk %s  " "\xE2\x80\xA2" "  hex %s", a->squawk[0] ? a->squawk : "----", a->hex);
        }
    }
    for (int i = 0; i < 6; i++) {
        ui_set_text(s_detail[i], l[i]);
    }

    if (s_emergency >= 0) {
        const adsb_aircraft_t *a = &s_slots[s_emergency].ac;
        char b[96];
        snprintf(b, sizeof(b), "SQUAWK %s  %s  %s", a->squawk, squawk_meaning(a->squawk),
                 a->flight[0] ? a->flight : a->hex);
        ui_set_text(s_banner, b);
    }
    ui_set_hidden(s_banner, s_emergency < 0);
}

static void on_range_clicked(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == s_range_idx) {
        return;
    }
    s_range_idx = idx;
    aircraft_set_range_nm(s_ranges[idx]);
    for (int i = 0; i < RANGE_COUNT; i++) {
        lv_obj_set_state(s_range_btn[i], LV_STATE_CHECKED, i == idx);
    }
    layout_static();
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].used) {
            layout_trail(&s_slots[i]);
        }
    }
    render();
    refresh_panel();
}

static void build_panel(lv_obj_t *screen)
{
    lv_obj_t *panel = plain_obj(screen);
    lv_obj_set_pos(panel, SCOPE, 0);
    lv_obj_set_size(panel, PANEL_W, SCOPE);
    lv_obj_set_style_bg_color(panel, C_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(panel, PANEL_PAD, 0);

    static const int y[6] = { 0, 44, 78, 108, 138, 172 };
    for (int i = 0; i < 6; i++) {
        const lv_font_t *f = i == 0 ? &lv_font_montserrat_28 : (i == 5 ? &lv_font_montserrat_14 : &lv_font_montserrat_20);
        s_detail[i] = ui_fixed_label(panel, f, CONTENT_W, i == 0 ? lv_color_white() : C_TEXT);
        lv_obj_set_pos(s_detail[i], 0, y[i]);
    }

    lv_obj_t *rl = text(panel, &lv_font_montserrat_14, C_DIM, "RANGE");
    lv_obj_set_pos(rl, 0, 226);
    for (int i = 0; i < RANGE_COUNT; i++) {
        lv_obj_t *b = lv_button_create(panel);
        lv_obj_set_size(b, 150, 56);
        lv_obj_set_pos(b, i * 168, 250);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x263238), 0);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x2E7D32), LV_STATE_CHECKED);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, on_range_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        char t[12];
        snprintf(t, sizeof(t), "%d nm", s_ranges[i]);
        lv_obj_center(text(b, &lv_font_montserrat_20, C_TEXT, t));
        lv_obj_set_state(b, LV_STATE_CHECKED, i == s_range_idx);
        s_range_btn[i] = b;
    }

    /* Altitude key, matching alt_color(). */
    static const struct { uint32_t c; const char *t; } key[] = {
        { 0xFF6D00, "<3k ft" }, { 0xFFD600, "3-10k" }, { 0x00E676, "10-25k" }, { 0x18FFFF, ">25k" },
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = text(panel, &lv_font_montserrat_14, lv_color_hex(key[i].c), "");
        lv_label_set_text_fmt(l, "\xE2\x80\xA2 %s", key[i].t);
        lv_obj_set_pos(l, i * 128, 330);
    }

    lv_obj_t *status = plain_obj(panel);
    lv_obj_set_size(status, CONTENT_W, LV_SIZE_CONTENT);
    lv_obj_align(status, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    status_corner_create(status, CONTENT_W);
}

static void frame_cb(lv_timer_t *t)
{
    static int frame;
    aircraft_meta_t m;
    int n = aircraft_snapshot(s_snap, ADSB_MAX_AIRCRAFT, &m);
    if (m.seq != s_seq) {
        s_seq = m.seq;
        ingest(n, &m);
    }
    render();
    if (++frame % PANEL_EVERY == 0) {
        refresh_panel();
        status_corner_refresh();
    }
}

void radar_create(lv_obj_t *screen)
{
    s_icons = icons_create();
    s_slots = heap_caps_calloc(MAX_SLOTS, sizeof(slot_t), MALLOC_CAP_SPIRAM);
    s_snap = heap_caps_calloc(ADSB_MAX_AIRCRAFT, sizeof(adsb_aircraft_t), MALLOC_CAP_SPIRAM);

    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(screen, false);

    s_scope = plain_obj(screen);
    lv_obj_set_size(s_scope, SCOPE, SCOPE);
    lv_obj_set_clickable(s_scope, true);
    lv_obj_add_event_cb(s_scope, on_scope_clicked, LV_EVENT_CLICKED, NULL);

    if (s_icons.plane == NULL || s_icons.blip == NULL || s_icons.arrow == NULL || s_slots == NULL || s_snap == NULL) {
        /* Loud, not fatal: say so on screen. */
        ESP_LOGE(TAG, "out of memory");
        lv_obj_center(text(s_scope, &lv_font_montserrat_28, lv_color_hex(C_EMERG), "Radar: out of memory"));
        return;
    }

    build_static();

    s_sel_ring = plain_obj(s_scope);
    lv_obj_set_size(s_sel_ring, ICON_SIZE + 12, ICON_SIZE + 12);
    lv_obj_set_style_radius(s_sel_ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(s_sel_ring, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_sel_ring, 2, 0);
    lv_obj_set_hidden(s_sel_ring, true);

    s_banner = lv_label_create(s_scope);
    lv_obj_set_size(s_banner, SCOPE, 40);
    lv_obj_set_style_bg_color(s_banner, lv_color_hex(C_EMERG), 0);
    lv_obj_set_style_bg_opa(s_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_banner, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_banner, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(s_banner, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(s_banner, 8, 0);
    lv_label_set_text(s_banner, "");
    lv_obj_set_hidden(s_banner, true);

    build_panel(screen);
    aircraft_set_range_nm(s_ranges[s_range_idx]);
    refresh_panel();
    lv_timer_create(frame_cb, FRAME_MS, NULL);
}
