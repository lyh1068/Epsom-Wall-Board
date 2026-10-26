#include "controls.h"

#include <stdio.h>
#include "config.h"
#include "sys/settings.h"
#include "ui/ui_util.h"

#define C_TEXT   lv_color_hex(0xECEFF1)
#define C_DIM    lv_color_hex(0x90A4AE)
#define C_PANEL  lv_color_hex(0x1E272E)
#define C_ACCENT lv_color_hex(0xFFB300)

static lv_obj_t *s_overlay, *s_slider, *s_value;

static void show_value(int percent)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%", percent);
    ui_set_text(s_value, buf);
}

static void close_cb(lv_event_t *e)
{
    /* Only taps on the dimmed background close it, not taps inside the panel. */
    if (lv_event_get_target(e) != s_overlay) {
        return;
    }
    settings_save();
    lv_obj_delete_async(s_overlay);
    s_overlay = NULL;
}

static void slider_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(s_slider);
    settings_set_brightness(v);
    show_value(v);
}

static void preset_cb(lv_event_t *e)
{
    int v = (int)(intptr_t)lv_event_get_user_data(e);
    lv_slider_set_value(s_slider, v, LV_ANIM_OFF);
    settings_set_brightness(v);
    show_value(v);
}

bool controls_is_open(void)
{
    return s_overlay != NULL;
}

void controls_open(void)
{
    if (s_overlay != NULL) {
        return;
    }
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_60, 0);
    lv_obj_set_clickable(s_overlay, true);
    lv_obj_add_event_cb(s_overlay, close_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(panel);
    lv_obj_set_scrollable(panel, false);
    lv_obj_set_size(panel, 760, 250);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 40);
    lv_obj_set_style_bg_color(panel, C_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 14, 0);
    lv_obj_set_style_pad_all(panel, 28, 0);
    lv_obj_set_clickable(panel, true);  /* swallow taps so they do not close it */

    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, C_TEXT, 0);
    lv_label_set_text(title, LV_SYMBOL_IMAGE "  Brightness");
    lv_obj_set_pos(title, 0, 0);

    s_value = ui_fixed_label(panel, &lv_font_montserrat_28, 120, C_ACCENT);
    lv_obj_set_style_text_align(s_value, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_value, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_slider = lv_slider_create(panel);
    lv_obj_set_size(s_slider, 660, 24);
    lv_obj_set_pos(s_slider, 12, 64);
    lv_slider_set_range(s_slider, BRIGHTNESS_MIN, 100);
    lv_slider_set_value(s_slider, settings_brightness(), LV_ANIM_OFF);
    lv_obj_set_ext_click_area(s_slider, 20);
    lv_obj_set_style_bg_color(s_slider, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_slider, C_ACCENT, LV_PART_KNOB);
    lv_obj_add_event_cb(s_slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    show_value(settings_brightness());

    static const int presets[] = { 10, 25, 50, 75, 100 };
    for (int i = 0; i < 5; i++) {
        lv_obj_t *b = lv_button_create(panel);
        lv_obj_set_size(b, 120, 52);
        lv_obj_set_pos(b, i * 140, 116);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x37474F), 0);
        lv_obj_set_style_shadow_width(b, 0, 0);
        lv_obj_add_event_cb(b, preset_cb, LV_EVENT_CLICKED, (void *)(intptr_t)presets[i]);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(l, C_TEXT, 0);
        lv_label_set_text_fmt(l, "%d%%", presets[i]);
        lv_obj_center(l);
    }

    lv_obj_t *hint = lv_label_create(panel);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(hint, C_DIM, 0);
    lv_label_set_text(hint, "Saved when you close this. Tap outside to close; swipe down anywhere to open.");
    lv_obj_set_pos(hint, 0, 180);
}
