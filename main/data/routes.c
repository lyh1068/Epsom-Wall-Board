#include "routes.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "config.h"
#include "data/route_cache.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "net/http_json.h"
#include "net/wifi.h"

static const char *TAG = "routes";

#define FS_BASE     "/data"
#define FS_LABEL    "storage"
#define CACHE_FILE  FS_BASE "/routes.bin"
#define CACHE_TMP   FS_BASE "/routes.tmp"
#define QUEUE_LEN   8
#define FAIL_SLOTS  8

typedef struct {
    char cs[9];
    int64_t retry_at_us;
} fail_t;

static SemaphoreHandle_t s_mutex;
static QueueHandle_t s_queue;
static route_cache_t s_cache;
static char s_pending[QUEUE_LEN][9];
static fail_t s_fail[FAIL_SLOTS];
static routes_meta_t s_meta;

#define LOCK()   xSemaphoreTake(s_mutex, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_mutex)

static uint32_t now_s(void)
{
    return (uint32_t)time(NULL);
}

/* Only plain callsigns go into a URL. */
static bool valid_callsign(const char *cs)
{
    size_t n = strlen(cs);
    if (n < 3 || n > 8) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!isupper((unsigned char)cs[i]) && !isdigit((unsigned char)cs[i])) {
            return false;
        }
    }
    return true;
}

static bool pending_has(const char *cs)
{
    for (int i = 0; i < QUEUE_LEN; i++) {
        if (strcmp(s_pending[i], cs) == 0) {
            return true;
        }
    }
    return false;
}

static bool pending_add(const char *cs)
{
    for (int i = 0; i < QUEUE_LEN; i++) {
        if (s_pending[i][0] == '\0') {
            strlcpy(s_pending[i], cs, sizeof(s_pending[i]));
            return true;
        }
    }
    return false;
}

static void pending_remove(const char *cs)
{
    for (int i = 0; i < QUEUE_LEN; i++) {
        if (strcmp(s_pending[i], cs) == 0) {
            s_pending[i][0] = '\0';
        }
    }
}

static fail_t *fail_find(const char *cs)
{
    for (int i = 0; i < FAIL_SLOTS; i++) {
        if (s_fail[i].cs[0] && strcmp(s_fail[i].cs, cs) == 0) {
            return &s_fail[i];
        }
    }
    return NULL;
}

static void fail_record(const char *cs, uint32_t backoff_s)
{
    fail_t *f = fail_find(cs);
    if (f == NULL) {
        f = &s_fail[0];
        for (int i = 0; i < FAIL_SLOTS; i++) {
            if (s_fail[i].cs[0] == '\0' || s_fail[i].retry_at_us < f->retry_at_us) {
                f = &s_fail[i];
            }
        }
    }
    strlcpy(f->cs, cs, sizeof(f->cs));
    f->retry_at_us = esp_timer_get_time() + (int64_t)backoff_s * 1000000;
}

route_state_t routes_lookup(const char *callsign, route_t *out)
{
    if (s_mutex == NULL || !valid_callsign(callsign)) {
        return ROUTE_ST_NO_CALLSIGN;
    }
    route_state_t st;
    LOCK();
    const rc_entry_t *e = rc_get(&s_cache, callsign, now_s(), ROUTES_TTL_FOUND_S, ROUTES_TTL_UNKNOWN_S);
    fail_t *f;
    if (e != NULL) {
        st = e->kind == RC_FOUND ? ROUTE_ST_FOUND : ROUTE_ST_UNKNOWN;
        if (st == ROUTE_ST_FOUND && out != NULL) {
            *out = e->route;
        }
    } else if (pending_has(callsign)) {
        st = ROUTE_ST_PENDING;
    } else if ((f = fail_find(callsign)) != NULL && esp_timer_get_time() < f->retry_at_us) {
        st = ROUTE_ST_ERROR;
    } else if (pending_add(callsign)) {
        char cs[9];
        strlcpy(cs, callsign, sizeof(cs));
        if (xQueueSend(s_queue, cs, 0) != pdTRUE) {
            pending_remove(callsign);
        }
        st = ROUTE_ST_PENDING;
    } else {
        st = ROUTE_ST_PENDING;      /* queue full: ask again next refresh */
    }
    UNLOCK();
    return st;
}

void routes_get_meta(routes_meta_t *m)
{
    if (s_mutex == NULL) {
        memset(m, 0, sizeof(*m));
        return;
    }
    LOCK();
    *m = s_meta;
    m->cached = rc_count(&s_cache);
    UNLOCK();
}

/* ---- persistence -------------------------------------------------------------- */

static void fs_mount_and_load(void)
{
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = FS_BASE,
        .partition_label = FS_LABEL,
        .format_if_mount_failed = true,     /* first boot, or a corrupt fs: start empty */
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    LOCK();
    s_meta.fs_mounted = err == ESP_OK;
    s_meta.fs_err = err;
    UNLOCK();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LittleFS mount failed: %s; cache is RAM-only", esp_err_to_name(err));
        return;
    }

    FILE *f = fopen(CACHE_FILE, "rb");
    if (f == NULL) {
        ESP_LOGI(TAG, "no saved cache yet");
        return;
    }
    struct stat st;
    uint8_t *buf = NULL;
    size_t len = 0;
    if (fstat(fileno(f), &st) == 0 && st.st_size > 0 && st.st_size < 1024 * 1024) {
        buf = heap_caps_malloc(st.st_size, MALLOC_CAP_SPIRAM);
        if (buf != NULL) {
            len = fread(buf, 1, st.st_size, f);
        }
    }
    fclose(f);
    if (buf == NULL) {
        return;
    }
    LOCK();
    bool ok = len == (size_t)st.st_size && rc_deserialize(&s_cache, buf, len);
    s_meta.loaded = ok;
    int n = rc_count(&s_cache);
    UNLOCK();
    heap_caps_free(buf);
    if (ok) {
        ESP_LOGI(TAG, "loaded %d cached routes", n);
    } else {
        ESP_LOGW(TAG, "saved cache unreadable; starting empty");
    }
}

/* Write to a temp file and rename over the old one, so a power cut mid-save
 * leaves the previous cache intact. */
static void save_if_due(void)
{
    LOCK();
    bool due = s_meta.fs_mounted && s_cache.dirty
               && (s_meta.last_save == 0 || time(NULL) - s_meta.last_save >= ROUTES_SAVE_EVERY_S);
    size_t size = due ? rc_serialized_size(&s_cache) : 0;
    UNLOCK();
    if (!due) {
        return;
    }
    uint8_t *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        return;
    }
    /* Returns 0 if the cache grew since we sized buf; it stays dirty for next time. */
    LOCK();
    size_t n = rc_serialize(&s_cache, buf, size);
    UNLOCK();

    bool ok = false;
    FILE *f = n ? fopen(CACHE_TMP, "wb") : NULL;
    if (f != NULL) {
        ok = fwrite(buf, 1, n, f) == n;
        ok = (fclose(f) == 0) && ok;
        ok = ok && rename(CACHE_TMP, CACHE_FILE) == 0;
    }
    heap_caps_free(buf);
    LOCK();
    if (ok) {
        s_meta.last_save = time(NULL);
    } else {
        s_cache.dirty = true;     /* try again next time */
    }
    UNLOCK();
    if (ok) {
        ESP_LOGI(TAG, "saved %u bytes", (unsigned)n);
    } else {
        ESP_LOGW(TAG, "save failed (%u bytes)", (unsigned)n);
    }
}

/* ---- fetching ----------------------------------------------------------------- */

static void fetch(http_json_t *http, const char *cs, uint32_t *backoff_s)
{
    char url[80];
    snprintf(url, sizeof(url), "https://api.adsbdb.com/v0/callsign/%s", cs);
    const char *body;
    size_t len;
    int status = 0;
    esp_err_t err = http_json_get(http, url, &body, &len, &status);

    route_t r;
    route_parse_result_t res = ROUTE_PARSE_ERROR;
    if (err == ESP_OK && (status == 200 || status == 404)) {
        res = route_parse(body, len, &r);
        if (res == ROUTE_PARSE_ERROR) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
    }

    LOCK();
    s_meta.http_status = status;
    s_meta.last_err = err;
    s_meta.rate_limited = false;
    if (res == ROUTE_PARSE_OK) {
        rc_put(&s_cache, cs, RC_FOUND, &r, now_s());
        *backoff_s = 0;
    } else if (res == ROUTE_PARSE_UNKNOWN) {
        rc_put(&s_cache, cs, RC_UNKNOWN, NULL, now_s());
        *backoff_s = 0;
    } else if (status == 429) {
        /* adsbdb counts requests per public IP over a 60 s window (512, then a
         * 5 min lockout past 1024), and a shared IP can use that up. Retrying
         * inside the window only adds to the count, so wait at least one. */
        *backoff_s = *backoff_s >= ROUTES_429_BACKOFF_S
                     ? (*backoff_s * 2 > ROUTES_BACKOFF_MAX_S ? ROUTES_BACKOFF_MAX_S : *backoff_s * 2)
                     : ROUTES_429_BACKOFF_S;
        s_meta.rate_limited = true;
        fail_record(cs, *backoff_s);
    } else {
        /* 5xx, transport: back off and let the UI ask again later. */
        *backoff_s = *backoff_s ? (*backoff_s * 2 > ROUTES_BACKOFF_MAX_S ? ROUTES_BACKOFF_MAX_S : *backoff_s * 2) : 10;
        fail_record(cs, *backoff_s);
    }
    s_meta.backoff_s = *backoff_s;
    s_meta.retry_at_us = esp_timer_get_time() + (int64_t)*backoff_s * 1000000;
    pending_remove(cs);
    UNLOCK();

    if (res == ROUTE_PARSE_ERROR) {
        ESP_LOGW(TAG, "%s: HTTP %d, %s; backoff %lu s", cs, status, esp_err_to_name(err), (unsigned long)*backoff_s);
    }
}

static void routes_task(void *arg)
{
    fs_mount_and_load();
    http_json_t *http = http_json_new(16 * 1024);
    uint32_t backoff_s = 0;
    int64_t last_req_us = 0;

    for (;;) {
        char cs[9];
        if (xQueueReceive(s_queue, cs, pdMS_TO_TICKS(30000)) == pdTRUE && http != NULL) {
            wifi_status_t ws;
            for (wifi_get_status(&ws); ws.state != WIFI_ST_CONNECTED; wifi_get_status(&ws)) {
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            /* Be a good citizen: space requests out, and wait out any backoff. */
            int64_t wait_ms = ROUTES_MIN_INTERVAL_MS - (esp_timer_get_time() - last_req_us) / 1000;
            if (backoff_s * 1000 > wait_ms) {
                wait_ms = backoff_s * 1000;
            }
            if (wait_ms > 0) {
                vTaskDelay(pdMS_TO_TICKS(wait_ms));
            }
            last_req_us = esp_timer_get_time();
            fetch(http, cs, &backoff_s);
        }
        save_if_due();
    }
}

void routes_start(void)
{
    rc_entry_t *store = heap_caps_calloc(ROUTES_CACHE_SIZE, sizeof(rc_entry_t), MALLOC_CAP_SPIRAM);
    s_mutex = xSemaphoreCreateMutex();
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(char[9]));
    if (store == NULL || s_mutex == NULL || s_queue == NULL) {
        ESP_LOGE(TAG, "out of memory; route lookups disabled");
        s_mutex = NULL;
        return;
    }
    rc_init(&s_cache, store, ROUTES_CACHE_SIZE);
    xTaskCreate(routes_task, "routes", 12288, NULL, 3, NULL);
}
