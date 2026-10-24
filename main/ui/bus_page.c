#include "bus_page.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_attr.h"
#include "config.h"
#include "data/bus.h"
#include "data/bus_map.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ui/ui_util.h"

static const char *TAG = "bus_page";

/*
 * 1280x720: title row; on the left a north-up street map (OpenStreetMap,
 * baked by tools/make_bus_map.py) with a pin per stop; on the right a 2x4
 * grid of stop cards, nearest first, each with its next arrivals. Pins and
 * cards carry the same red letter, as on the stop flags.
 */
#define PAD         16
#define TITLE_H     56
#define MAP_X       PAD
#define MAP_Y       (TITLE_H + 8)
#define MAP         BUS_MAP_SIZE
#define GRID_X      (MAP_X + MAP + PAD)
#define GRID_W      (1280 - GRID_X - PAD)
#define COLS        2
#define ROWS        4
#define CARD_GAP    10
#define CARD_W      ((GRID_W - CARD_GAP) / COLS)
#define CARD_H      ((MAP - CARD_GAP * (ROWS - 1)) / ROWS)
#define ROW_H       26
#define PIN         26
#define REFRESH_MS  1000

#define C_TEXT      lv_color_hex(0xECEFF1)
#define C_DIM       lv_color_hex(0x90A4AE)
#define C_AMBER     lv_color_hex(0xFFB000)
#define C_CARD      lv_color_hex(0x10171C)
#define C_LETTER    lv_color_hex(0xC62828)

typedef struct {
    lv_obj_t *line[BUS_PER_STOP], *dest[BUS_PER_STOP], *wait[BUS_PER_STOP];
} card_t;

static card_t s_cards[BUS_MAX_STOPS];
static lv_obj_t *s_note;

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t c, int32_t x, int32_t y, int32_t w)
{
    lv_obj_t *l = ui_fixed_label(parent, font, w, c);
    lv_obj_set_pos(l, x, y);
    return l;
}

/* A stop letter in a red disc: on the map pins and on the cards. */
static lv_obj_t *stop_flag(lv_obj_t *parent, const char *letter, int32_t size, const lv_font_t *font)
{
    lv_obj_t *f = lv_label_create(parent);
    lv_label_set_text(f, letter);
    lv_obj_set_size(f, size, size);
    lv_obj_set_style_radius(f, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(f, C_LETTER, 0);
    lv_obj_set_style_bg_opa(f, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(f, lv_color_white(), 0);
    lv_obj_set_style_border_width(f, 2, 0);
    lv_obj_set_style_text_color(f, lv_color_white(), 0);
    lv_obj_set_style_text_font(f, font, 0);
    lv_obj_set_style_text_align(f, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(f, (size - lv_font_get_line_height(font)) / 2 - 2, 0);
    return f;
}

/* ---- the map ------------------------------------------------------------------------ */

static uint16_t *s_map;

static inline void blend(int x, int y, lv_color_t c, float a)
{
    if (x < 0 || y < 0 || x >= MAP || y >= MAP || a <= 0) {
        return;
    }
    a = a > 1 ? 1 : a;
    uint16_t *p = &s_map[y * MAP + x];
    int r = ((*p >> 11) & 0x1F) << 3, g = ((*p >> 5) & 0x3F) << 2, b = (*p & 0x1F) << 3;
    *p = lv_color_to_u16(lv_color_make((uint8_t)(r + (c.red - r) * a), (uint8_t)(g + (c.green - g) * a),
                                       (uint8_t)(b + (c.blue - b) * a)));
}

/* Antialiased thick segment: coverage from each pixel's distance to the segment. */
static void segment(float x0, float y0, float x1, float y1, float w, lv_color_t c)
{
    float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
    int xa = (int)floorf(fminf(x0, x1) - w), xb = (int)ceilf(fmaxf(x0, x1) + w);
    int ya = (int)floorf(fminf(y0, y1) - w), yb = (int)ceilf(fmaxf(y0, y1) + w);
    xa = xa < 0 ? 0 : xa;
    ya = ya < 0 ? 0 : ya;
    xb = xb >= MAP ? MAP - 1 : xb;
    yb = yb >= MAP ? MAP - 1 : yb;
    for (int y = ya; y <= yb; y++) {
        for (int x = xa; x <= xb; x++) {
            float t = l2 > 0 ? ((x - x0) * dx + (y - y0) * dy) / l2 : 0;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            float ex = x - (x0 + t * dx), ey = y - (y0 + t * dy);
            blend(x, y, c, w / 2 + 0.5f - sqrtf(ex * ex + ey * ey));
        }
    }
}

static void draw_map(void)
{
    const uint16_t bg = lv_color_to_u16(lv_color_hex(0x0E1418));
    for (int i = 0; i < MAP * MAP; i++) {
        s_map[i] = bg;
    }
    /* Width and colour per class, minor roads first (the header is sorted that way). */
    static const struct { float w; uint32_t c; } STYLE[] = {
        [MAP_MAJOR] = { 7, 0xC8B27A }, [MAP_SECONDARY] = { 5, 0x9AA5AE }, [MAP_MINOR] = { 3, 0x6E7A84 },
        [MAP_SERVICE] = { 2, 0x4A555E }, [MAP_PATH] = { 1, 0x3C464E }, [MAP_RAIL] = { 3, 0x9678C8 },
        [MAP_PLATFORM] = { 5, 0x5C4F7A },
    };
    for (int i = 0; i < BUS_MAP_POLY_COUNT; i++) {
        const bus_map_poly_t *p = &BUS_MAP_POLYS[i];
        lv_color_t c = lv_color_hex(STYLE[p->cls].c);
        for (int k = 1; k < p->count; k++) {
            const int16_t *a = BUS_MAP_POINTS[p->first + k - 1], *b = BUS_MAP_POINTS[p->first + k];
            segment(a[0], a[1], b[0], b[1], STYLE[p->cls].w, c);
        }
    }
    /* The 300 m circle, dashed. */
    const float r300 = 300 * BUS_MAP_SCALE;
    for (int k = 0; k < 1440; k++) {
        if (k % 16 < 8) {
            float a = k * (float)M_PI / 720;
            blend((int)(MAP / 2 + r300 * cosf(a)), (int)(MAP / 2 + r300 * sinf(a)), lv_color_hex(0x5A7D96), 1);
        }
    }
}

static void map_xy(float lat, float lon, int32_t *x, int32_t *y)
{
    float mx = (lon - BUS_MAP_LON) * 111320.0f * cosf(BUS_MAP_LAT * (float)M_PI / 180);
    float my = (lat - BUS_MAP_LAT) * 110540.0f;
    *x = (int32_t)lroundf(MAP / 2 + mx * BUS_MAP_SCALE);
    *y = (int32_t)lroundf(MAP / 2 - my * BUS_MAP_SCALE);
}

static void build_map(lv_obj_t *page)
{
    /* ~770 KB, so PSRAM; drawn once, it never changes. */
    s_map = heap_caps_aligned_alloc(64, MAP * MAP * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (s_map == NULL) {
        ESP_LOGE(TAG, "no PSRAM for the map");
        lv_label_set_text(label(page, &lv_font_montserrat_20, lv_color_hex(0xFF5252), MAP_X + 20, MAP_Y + 280, 400),
                          "Map: out of memory");
        return;
    }
    draw_map();
    lv_obj_t *canvas = lv_canvas_create(page);
    lv_canvas_set_buffer(canvas, s_map, MAP, MAP, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, MAP_X, MAP_Y);
    lv_obj_set_clickable(canvas, false);

    for (int i = 0; i < BUS_MAP_LABEL_COUNT; i++) {
        lv_obj_t *l = label(page, &lv_font_montserrat_14, lv_color_hex(0xB0BEC5),
                            MAP_X + BUS_MAP_LABELS[i].x + 6, MAP_Y + BUS_MAP_LABELS[i].y - 18, 150);
        lv_label_set_text(l, BUS_MAP_LABELS[i].name);
    }

    /* The station: a marker at the centre of the map. */
    lv_obj_t *st = lv_obj_create(page);
    lv_obj_remove_style_all(st);
    lv_obj_set_size(st, 16, 16);
    lv_obj_set_pos(st, MAP_X + MAP / 2 - 8, MAP_Y + MAP / 2 - 8);
    lv_obj_set_style_radius(st, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(st, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(st, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(st, lv_color_hex(0x9678C8), 0);
    lv_obj_set_style_border_width(st, 4, 0);
    lv_obj_t *sl = label(page, &lv_font_montserrat_14, lv_color_white(), MAP_X + MAP / 2 - 60, MAP_Y + MAP / 2 - 30, 140);
    lv_label_set_text(sl, "Epsom station");

    for (int i = 0; i < BUS_STOP_COUNT; i++) {
        int32_t x, y;
        map_xy(BUS_STOPS[i].lat, BUS_STOPS[i].lon, &x, &y);
        lv_obj_t *pin = stop_flag(page, BUS_STOPS[i].letter, PIN, &lv_font_montserrat_14);
        lv_obj_set_pos(pin, MAP_X + x - PIN / 2, MAP_Y + y - PIN / 2);
    }

    lv_obj_t *n = label(page, &lv_font_montserrat_20, C_AMBER, MAP_X + 10, MAP_Y + 8, 60);
    lv_label_set_text(n, LV_SYMBOL_UP " N");
    lv_obj_t *r = label(page, &lv_font_montserrat_14, lv_color_hex(0x5A7D96), MAP_X + MAP / 2 + 8,
                        MAP_Y + MAP / 2 - (int32_t)(300 * BUS_MAP_SCALE) + 4, 80);
    lv_label_set_text(r, "300 m");
    lv_obj_t *credit = label(page, &lv_font_montserrat_14, C_DIM, MAP_X + MAP - 250, MAP_Y + MAP - 22, 244);
    lv_obj_set_style_text_align(credit, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(credit, "Map (c) OpenStreetMap contributors");
}

/* ---- the cards ---------------------------------------------------------------------- */

static void set_row(card_t *c, int r, const char *line, const char *dest, const char *wait)
{
    ui_set_text(c->line[r], line);
    ui_set_text(c->dest[r], dest);
    ui_set_text(c->wait[r], wait);
}

static void refresh_cb(lv_timer_t *t)
{
    static EXT_RAM_BSS_ATTR bus_board_t b;   /* off the LVGL stack, and out of internal RAM */
    const int64_t now = esp_timer_get_time();

    for (int i = 0; i < BUS_STOP_COUNT; i++) {
        card_t *c = &s_cards[i];
        bus_stop_meta_t sm;
        int r = 0;
        if (!bus_get_stop(i, &b, &sm)) {
            set_row(c, r++, "", "waiting...", "");
        } else {
            /* Count down locally between this stop's polls. */
            int elapsed = (int)((now - sm.fetched_us) / 1000000);
            for (int k = 0; k < b.count && r < BUS_PER_STOP; k++) {
                int secs = b.a[k].secs - elapsed;
                if (secs < -60) {
                    continue;   /* gone; the next poll drops it */
                }
                char wait[12];
                bus_fmt_wait(wait, sizeof(wait), secs);
                set_row(c, r++, b.a[k].line, b.a[k].dest, wait);
            }
            if (r == 0) {
                set_row(c, r++, "", "No buses due", "");
            }
        }
        for (; r < BUS_PER_STOP; r++) {
            set_row(c, r, "", "", "");
        }
    }

    bus_meta_t m;
    bus_get_meta(&m);
    char note[96];
    if (m.waiting_for_wifi) {
        snprintf(note, sizeof(note), "Waiting for Wi-Fi");
    } else if (m.oldest_s < 0) {
        snprintf(note, sizeof(note), "Loading stops...");
    } else {
        snprintf(note, sizeof(note), "TfL routes only  " "\xE2\x80\xA2" "  each stop updated every %d s", BUS_POLL_S);
    }
    ui_set_text(s_note, note);
}

void bus_page_create(lv_obj_t *page)
{
    lv_obj_set_style_bg_color(page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(page, false);

    lv_obj_t *title = label(page, &lv_font_montserrat_28, C_AMBER, PAD + 4, 14, 640);
    lv_label_set_text(title, "Buses within 300 m of Epsom station");
    s_note = label(page, &lv_font_montserrat_14, C_DIM, 700, 24, 1280 - 700 - PAD - 4);
    lv_obj_set_style_text_align(s_note, LV_TEXT_ALIGN_RIGHT, 0);

    build_map(page);

    for (int i = 0; i < BUS_STOP_COUNT; i++) {
        const bus_stop_t *st = &BUS_STOPS[i];
        lv_obj_t *card = lv_obj_create(page);
        lv_obj_remove_style_all(card);
        lv_obj_set_scrollable(card, false);
        lv_obj_set_clickable(card, false);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_pos(card, GRID_X + (i % COLS) * (CARD_W + CARD_GAP), MAP_Y + (i / COLS) * (CARD_H + CARD_GAP));
        lv_obj_set_style_bg_color(card, C_CARD, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 10, 0);

        lv_obj_t *flag = stop_flag(card, st->letter, 30, &lv_font_montserrat_20);
        lv_obj_set_pos(flag, 8, 8);
        lv_obj_t *name = label(card, &lv_font_montserrat_20, C_TEXT, 46, 6, CARD_W - 46 - 56);
        lv_label_set_text(name, st->name);
        lv_obj_t *dist = label(card, &lv_font_montserrat_14, C_DIM, CARD_W - 56, 10, 48);
        lv_label_set_text_fmt(dist, "%d m", st->distance_m);
        lv_obj_set_style_text_align(dist, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_t *routes = label(card, &lv_font_montserrat_14, C_DIM, 46, 28, CARD_W - 54);
        lv_label_set_text(routes, st->routes);

        card_t *c = &s_cards[i];
        for (int r = 0; r < BUS_PER_STOP; r++) {
            int32_t y = 50 + r * ROW_H;
            c->line[r] = label(card, &lv_font_montserrat_20, C_TEXT, 10, y, 48);
            c->dest[r] = label(card, &lv_font_montserrat_20, C_TEXT, 60, y, CARD_W - 60 - 76);
            c->wait[r] = label(card, &lv_font_montserrat_20, C_AMBER, CARD_W - 76, y, 66);
            lv_obj_set_style_text_align(c->wait[r], LV_TEXT_ALIGN_RIGHT, 0);
        }
    }

    refresh_cb(NULL);
    lv_timer_create(refresh_cb, REFRESH_MS, NULL);
}
