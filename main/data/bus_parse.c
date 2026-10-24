#include "bus_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "data/text_fold.h"

static void get_str(char *dst, size_t size, const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring != NULL) {
        text_fold_ascii(dst, size, item->valuestring);
    } else {
        dst[0] = '\0';
    }
}

static int by_secs(const void *a, const void *b)
{
    return ((const bus_arrival_t *)a)->secs - ((const bus_arrival_t *)b)->secs;
}

bool bus_parse(const char *json, size_t len, bus_board_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }

    /* Collect all, sort, then keep the soonest: the response order is arbitrary,
     * so truncating first could drop the next bus. */
    int n = cJSON_GetArraySize(root);
    bus_arrival_t *all = n > 0 ? calloc((size_t)n, sizeof(*all)) : NULL;
    int m = 0;
    const cJSON *p;
    cJSON_ArrayForEach(p, root) {
        const cJSON *t = cJSON_GetObjectItemCaseSensitive(p, "timeToStation");
        if (all == NULL || !cJSON_IsNumber(t)) {
            continue;
        }
        bus_arrival_t *a = &all[m++];
        get_str(a->line, sizeof(a->line), p, "lineName");
        get_str(a->dest, sizeof(a->dest), p, "destinationName");
        a->secs = (int)t->valuedouble;
        if (out->stop_name[0] == '\0') {
            get_str(out->stop_name, sizeof(out->stop_name), p, "stationName");
            get_str(out->stop_letter, sizeof(out->stop_letter), p, "platformName");
        }
    }
    if (m > 0) {
        qsort(all, (size_t)m, sizeof(*all), by_secs);
        out->count = m < BUS_MAX ? m : BUS_MAX;
        memcpy(out->a, all, (size_t)out->count * sizeof(*all));
    }
    free(all);
    cJSON_Delete(root);
    return true;
}

void bus_fmt_wait(char *buf, size_t size, int secs)
{
    int mins = secs / 60;
    if (mins < 1) {
        snprintf(buf, size, "due");
    } else {
        snprintf(buf, size, "%d min", mins);
    }
}
