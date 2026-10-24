#pragma once

#include <stdio.h>
#include <string.h>
#include "lvgl.h"

/* 3400 -> "3,400" */
static inline void ui_fmt_thousands(char *buf, size_t size, int v)
{
    if (v >= 1000) {
        snprintf(buf, size, "%d,%03d", v / 1000, v % 1000);
    } else {
        snprintf(buf, size, "%d", v);
    }
}

/*
 * LVGL redraws a label whenever its text is set, even to the same string, and
 * on a rotated single-framebuffer display every redraw is a chance to tear
 * (tab5-board skill, display.md). Periodic updates go through these.
 */
static inline void ui_set_text(lv_obj_t *label, const char *text)
{
    const char *cur = lv_label_get_text(label);
    if (cur == NULL || strcmp(cur, text) != 0) {
        lv_label_set_text(label, text);
    }
}

static inline void ui_set_text_color(lv_obj_t *obj, lv_color_t c)
{
    if (!lv_color_eq(lv_obj_get_style_text_color(obj, LV_PART_MAIN), c)) {
        lv_obj_set_style_text_color(obj, c, 0);
    }
}

static inline void ui_set_hidden(lv_obj_t *obj, bool hidden)
{
    if (lv_obj_is_hidden(obj) != hidden) {
        lv_obj_set_hidden(obj, hidden);
    }
}

/* A label with a pinned width, so a text change never moves its geometry. */
static inline lv_obj_t *ui_fixed_label(lv_obj_t *parent, const lv_font_t *font, int32_t w, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, "");
    return l;
}
