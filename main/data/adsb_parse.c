#include "adsb_parse.h"

#include <string.h>
#include "cJSON.h"

static void copy_str(char *dst, size_t size, const cJSON *item)
{
    dst[0] = '\0';
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        return;
    }
    strncpy(dst, item->valuestring, size - 1);
    dst[size - 1] = '\0';
    /* Callsigns are space-padded to 8. */
    for (size_t n = strlen(dst); n > 0 && dst[n - 1] == ' '; n--) {
        dst[n - 1] = '\0';
    }
}

static bool get_num(const cJSON *obj, const char *key, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

int adsb_parse(const char *json, size_t len, adsb_aircraft_t *out, int max, adsb_meta_t *meta)
{
    memset(meta, 0, sizeof(*meta));
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        return -1;
    }
    const cJSON *ac = cJSON_GetObjectItemCaseSensitive(root, "ac");
    if (!cJSON_IsArray(ac)) {
        cJSON_Delete(root);
        return -1;
    }

    double now = 0;
    if (get_num(root, "now", &now)) {
        meta->now_ms = (int64_t)now;
    }

    int n = 0;
    const cJSON *a;
    cJSON_ArrayForEach(a, ac) {
        meta->total++;
        adsb_aircraft_t *p = &out[n];
        double lat, lon, v;
        if (!get_num(a, "lat", &lat) || !get_num(a, "lon", &lon)) {
            meta->skipped++;
            continue;
        }
        if (n >= max) {
            continue;
        }
        memset(p, 0, sizeof(*p));
        p->lat = lat;
        p->lon = lon;
        copy_str(p->hex, sizeof(p->hex), cJSON_GetObjectItemCaseSensitive(a, "hex"));
        copy_str(p->flight, sizeof(p->flight), cJSON_GetObjectItemCaseSensitive(a, "flight"));
        copy_str(p->type, sizeof(p->type), cJSON_GetObjectItemCaseSensitive(a, "t"));
        copy_str(p->reg, sizeof(p->reg), cJSON_GetObjectItemCaseSensitive(a, "r"));
        copy_str(p->squawk, sizeof(p->squawk), cJSON_GetObjectItemCaseSensitive(a, "squawk"));

        /* alt_baro is a number in feet, or the string "ground". */
        const cJSON *alt = cJSON_GetObjectItemCaseSensitive(a, "alt_baro");
        if (cJSON_IsNumber(alt)) {
            p->alt_baro = (int32_t)alt->valuedouble;
            p->has_alt = true;
        } else if (cJSON_IsString(alt) && strcmp(alt->valuestring, "ground") == 0) {
            p->on_ground = true;
        }
        if (get_num(a, "gs", &v)) {
            p->gs = (float)v;
            p->has_gs = true;
        }
        if (get_num(a, "track", &v)) {
            p->track = (float)v;
            p->has_track = true;
        }
        if (get_num(a, "baro_rate", &v)) {
            p->baro_rate = (int32_t)v;
            p->has_rate = true;
        }
        if (get_num(a, "seen_pos", &v)) {
            p->seen_pos = (float)v;
        }
        n++;
    }

    cJSON_Delete(root);
    return n;
}

bool adsb_is_emergency_squawk(const char *squawk)
{
    return strcmp(squawk, "7500") == 0 || strcmp(squawk, "7600") == 0 || strcmp(squawk, "7700") == 0;
}
