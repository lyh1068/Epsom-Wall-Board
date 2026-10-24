#include "bus_page.h"

#include <stdio.h>
#include "esp_attr.h"
#include "config.h"
#include "data/bus.h"
#include "esp_timer.h"
#include "ui/ui_util.h"

/*
 * 1280x720: title row, then a 2x4 grid of stop cards in distance order.
 * Each card: letter + name + distance, the routes calling there, and the next
 * BUS_PER_STOP arrivals as route, destination, minutes.
 */
#define PAD         16
#define TITLE_H     52
#define COLS        2
#define CARD_W      ((1280 - PAD * (COLS + 1)) / COLS)
#define CARD_H      152
#define CARD_GAP    10
#define ROW_H       34
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

    lv_obj_t *title = label(page, &lv_font_montserrat_28, C_AMBER, PAD + 8, 12, 640);
    lv_label_set_text(title, "Buses within 300 m of Epsom station");
    s_note = label(page, &lv_font_montserrat_14, C_DIM, 680, 22, 1280 - 680 - PAD - 8);
    lv_obj_set_style_text_align(s_note, LV_TEXT_ALIGN_RIGHT, 0);

    for (int i = 0; i < BUS_STOP_COUNT; i++) {
        const bus_stop_t *st = &BUS_STOPS[i];
        lv_obj_t *card = lv_obj_create(page);
        lv_obj_remove_style_all(card);
        lv_obj_set_scrollable(card, false);
        lv_obj_set_clickable(card, false);
        lv_obj_set_size(card, CARD_W, CARD_H);
        lv_obj_set_pos(card, PAD + (i % COLS) * (CARD_W + PAD), TITLE_H + (i / COLS) * (CARD_H + CARD_GAP));
        lv_obj_set_style_bg_color(card, C_CARD, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 10, 0);

        /* Stop flag: the letter in a red disc, like the real ones. */
        lv_obj_t *flag = lv_label_create(card);
        lv_label_set_text(flag, st->letter);
        lv_obj_set_size(flag, 36, 36);
        lv_obj_set_pos(flag, 12, 10);
        lv_obj_set_style_radius(flag, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(flag, C_LETTER, 0);
        lv_obj_set_style_bg_opa(flag, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(flag, lv_color_white(), 0);
        lv_obj_set_style_text_font(flag, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_align(flag, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(flag, 7, 0);

        lv_obj_t *name = label(card, &lv_font_montserrat_20, C_TEXT, 58, 8, CARD_W - 58 - 90);
        lv_label_set_text(name, st->name);
        lv_obj_t *dist = label(card, &lv_font_montserrat_14, C_DIM, CARD_W - 90, 12, 78);
        lv_label_set_text_fmt(dist, "%d m", st->distance_m);
        lv_obj_set_style_text_align(dist, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_t *routes = label(card, &lv_font_montserrat_14, C_DIM, 58, 32, CARD_W - 70);
        lv_label_set_text(routes, st->routes);

        card_t *c = &s_cards[i];
        for (int r = 0; r < BUS_PER_STOP; r++) {
            int32_t y = 50 + r * ROW_H;
            c->line[r] = label(card, &lv_font_montserrat_28, C_TEXT, 14, y, 80);
            c->dest[r] = label(card, &lv_font_montserrat_28, C_TEXT, 98, y, CARD_W - 98 - 130);
            c->wait[r] = label(card, &lv_font_montserrat_28, C_AMBER, CARD_W - 130, y, 116);
            lv_obj_set_style_text_align(c->wait[r], LV_TEXT_ALIGN_RIGHT, 0);
        }
    }

    refresh_cb(NULL);
    lv_timer_create(refresh_cb, REFRESH_MS, NULL);
}
