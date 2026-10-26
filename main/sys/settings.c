#include "settings.h"

#include <stdbool.h>
#include "bsp/esp-bsp.h"
#include "config.h"
#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#define NS          "ui"
#define KEY_BRIGHT  "bright"

static int s_brightness = BRIGHTNESS_DEFAULT;
static int s_saved = -1;

void settings_init(void)
{
    /* The Wi-Fi task initialises NVS too; a second init just returns ESP_OK. */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    nvs_handle_t h;
    if (err != ESP_OK || nvs_open(NS, NVS_READONLY, &h) != ESP_OK) {
        return;     /* nothing saved yet, or no NVS: defaults */
    }
    uint8_t v;
    if (nvs_get_u8(h, KEY_BRIGHT, &v) == ESP_OK && v >= BRIGHTNESS_MIN && v <= 100) {
        s_brightness = v;
        s_saved = v;
    }
    nvs_close(h);
}

int settings_brightness(void)
{
    return s_brightness;
}

void settings_set_brightness(int percent)
{
    percent = percent < BRIGHTNESS_MIN ? BRIGHTNESS_MIN : (percent > 100 ? 100 : percent);
    s_brightness = percent;
    bsp_display_brightness_set(percent);
}

void settings_save(void)
{
    if (s_brightness == s_saved) {
        return;
    }
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed; brightness not saved");
        return;
    }
    if (nvs_set_u8(h, KEY_BRIGHT, (uint8_t)s_brightness) == ESP_OK && nvs_commit(h) == ESP_OK) {
        s_saved = s_brightness;
    }
    nvs_close(h);
}
