/*
 * SPDX-FileCopyrightText: 2021-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 */

/**
 * @file
 * @brief Epsom Wall Board entry point
 * @details Started from the BSP display example; the LVGL demo is still the
 *          screen content until Phase 1 replaces it.
 */

#include <stdio.h>
#include "bsp/esp-bsp.h"
#include "lvgl.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "diag.h"
#include "net/sntp_time.h"
#include "net/wifi.h"

extern void example_lvgl_demo_ui(lv_obj_t *scr);

void app_main(void)
{
    /* First, before the display: esp_hosted is already probing for the C6. */
    wifi_power_on();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    wifi_start();

    diag_capture_begin();
    bsp_display_start();
    diag_capture_end();

    time_init();
    time_sync_start();

    ESP_LOGI("example", "Display LVGL animation");
    bsp_display_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(NULL);
    lv_indev_t *indev = bsp_display_get_input_dev();
#if (CONFIG_ESP_LCD_TOUCH_MAX_POINTS > 1 && CONFIG_LV_USE_GESTURE_RECOGNITION)
    lv_indev_set_rotation_rad_threshold(indev, 0.15f);
#endif
    example_lvgl_demo_ui(scr);
    diag_overlay_create(indev);

    bsp_display_unlock();
    bsp_display_backlight_on();
}
