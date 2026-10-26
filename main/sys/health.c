#include "health.h"

#include <string.h>
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_core_dump.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "health";

/* Survives a crash restart, not a power cut: a magic marks it as valid. */
#define NOINIT_MAGIC 0x45505348u
static __NOINIT_ATTR uint32_t s_magic;
static __NOINIT_ATTR uint32_t s_boots;
static __NOINIT_ATTR uint32_t s_crashes;

static health_boot_t s_boot;
static uint32_t s_lvgl_free = UINT32_MAX;

/*
 * Display underruns (the blue frame). The DPI driver reports one only by a ROM
 * printf from its interrupt handler, and ROM printf also writes to a second
 * output channel, unused on this board: watch that for the word. IRAM and DRAM
 * only, as it runs inside the ISR.
 */
static const DRAM_ATTR char UNDERRUN[] = "underrun";
static DRAM_ATTR int s_match;
static volatile DRAM_ATTR uint32_t s_underruns;
static volatile DRAM_ATTR int64_t s_underrun_at_us;

static void IRAM_ATTR rom_putc_watch(char c)
{
    if (c == UNDERRUN[s_match]) {
        if (UNDERRUN[++s_match] == '\0') {
            s_underrun_at_us = esp_timer_get_time();
            s_underruns++;
            s_match = 0;
        }
    } else {
        s_match = c == UNDERRUN[0];
    }
}

static const char *reason_str(esp_reset_reason_t r, bool *crashed)
{
    *crashed = true;
    switch (r) {
    case ESP_RST_PANIC:    return "crash";
    case ESP_RST_INT_WDT:  return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "brownout (power dip)";
    default:               break;
    }
    *crashed = false;
    switch (r) {
    case ESP_RST_POWERON:  return "power-on";
    case ESP_RST_SW:       return "software restart";
    case ESP_RST_USB:      return "USB (flash or port opened)";
    case ESP_RST_EXT:      return "reset pin";
    case ESP_RST_DEEPSLEEP: return "wake from sleep";
    default:               return "unknown";
    }
}

void health_init(void)
{
    esp_reset_reason_t r = esp_reset_reason();
    s_boot.reason = reason_str(r, &s_boot.crashed);

    if (r == ESP_RST_POWERON || s_magic != NOINIT_MAGIC) {
        s_magic = NOINIT_MAGIC;
        s_boots = 0;
        s_crashes = 0;
    }
    s_boots++;
    if (s_boot.crashed) {
        s_crashes++;
    }
    s_boot.boots = s_boots;
    s_boot.crashes = s_crashes;

    esp_core_dump_summary_t sum;
    if (esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(&sum) == ESP_OK) {
        s_boot.dump = true;
        strlcpy(s_boot.dump_task, sum.exc_task, sizeof(s_boot.dump_task));
        s_boot.dump_pc = sum.exc_pc;
        char sha[APP_ELF_SHA256_SZ];
        esp_app_get_elf_sha256(sha, sizeof(sha));
        s_boot.dump_this_fw = strncmp(sha, (const char *)sum.app_elf_sha256, sizeof(sha) - 1) == 0;
        ESP_LOGW(TAG, "core dump: task %s, PC 0x%08lx%s", s_boot.dump_task, (unsigned long)s_boot.dump_pc,
                 s_boot.dump_this_fw ? "" : " (older firmware)");
    }
    esp_rom_install_channel_putc(2, rom_putc_watch);
    ESP_LOGI(TAG, "reset: %s, boot %lu, crashes %lu", s_boot.reason, (unsigned long)s_boots, (unsigned long)s_crashes);
}

const health_boot_t *health_boot(void)
{
    return &s_boot;
}

void health_sample_lvgl(void)
{
    /* High-water mark is in bytes on ESP-IDF (stack depth is bytes there). */
    uint32_t free = uxTaskGetStackHighWaterMark(NULL);
    if (free < s_lvgl_free) {
        s_lvgl_free = free;
    }
}

uint32_t health_lvgl_stack_free(void)
{
    return s_lvgl_free;
}

size_t health_internal_free(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

size_t health_internal_min_free(void)
{
    return heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
}

uint32_t health_uptime_s(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000000);
}

uint32_t health_display_underruns(uint32_t *ago_s)
{
    uint32_t n = s_underruns;
    if (ago_s != NULL) {
        *ago_s = n ? (uint32_t)((esp_timer_get_time() - s_underrun_at_us) / 1000000) : 0;
    }
    return n;
}
