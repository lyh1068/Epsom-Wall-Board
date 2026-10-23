#include "weather_parse.h"

#include <string.h>
#include "cJSON.h"

static bool get_num(const cJSON *obj, const char *key, double *out)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    *out = item->valuedouble;
    return true;
}

bool weather_parse(const char *json, size_t len, weather_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (root == NULL) {
        return false;
    }
    bool ok = false;
    const cJSON *cur = cJSON_GetObjectItemCaseSensitive(root, "current");
    const cJSON *hourly = cJSON_GetObjectItemCaseSensitive(root, "hourly");
    const cJSON *ht = cJSON_GetObjectItemCaseSensitive(hourly, "time");
    const cJSON *htemp = cJSON_GetObjectItemCaseSensitive(hourly, "temperature_2m");
    const cJSON *hpop = cJSON_GetObjectItemCaseSensitive(hourly, "precipitation_probability");
    const cJSON *hcode = cJSON_GetObjectItemCaseSensitive(hourly, "weather_code");
    double t, temp, code, day = 1;

    if (get_num(cur, "time", &t) && get_num(cur, "temperature_2m", &temp) && get_num(cur, "weather_code", &code)
        && cJSON_IsArray(ht) && cJSON_IsArray(htemp) && cJSON_IsArray(hpop) && cJSON_IsArray(hcode)) {
        get_num(cur, "is_day", &day);
        out->time = (time_t)t;
        out->temp_c = (float)temp;
        out->code = (int)code;
        out->is_day = day != 0;

        int n = cJSON_GetArraySize(ht);
        n = n > WX_HOURS ? WX_HOURS : n;
        for (int i = 0; i < n; i++) {
            const cJSON *a = cJSON_GetArrayItem(ht, i);
            const cJSON *b = cJSON_GetArrayItem(htemp, i);
            const cJSON *p = cJSON_GetArrayItem(hpop, i);
            const cJSON *c = cJSON_GetArrayItem(hcode, i);
            if (!cJSON_IsNumber(a) || !cJSON_IsNumber(b)) {
                break;      /* a gap in time or temperature ends the usable forecast */
            }
            out->h_time[i] = (time_t)a->valuedouble;
            out->h_temp[i] = (float)b->valuedouble;
            out->h_pop[i] = cJSON_IsNumber(p) ? (int8_t)p->valuedouble : -1;
            out->h_code[i] = cJSON_IsNumber(c) ? (int16_t)c->valuedouble : -1;
            out->hours = i + 1;
        }
        ok = out->hours > 0;
    }
    cJSON_Delete(root);
    return ok;
}

int weather_hour_index(const weather_t *w, time_t now)
{
    for (int i = 0; i < w->hours; i++) {
        if (now >= w->h_time[i] && now < w->h_time[i] + 3600) {
            return i;
        }
    }
    return -1;
}

int weather_next_rain(const weather_t *w, time_t now, int threshold_pct, int horizon_h)
{
    int start = weather_hour_index(w, now);
    if (start < 0) {
        return -1;
    }
    for (int i = start; i < w->hours && i < start + horizon_h; i++) {
        if (w->h_pop[i] >= threshold_pct) {
            return i;
        }
    }
    return -1;
}

const char *weather_code_text(int code)
{
    switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45:
    case 48: return "Fog";
    case 51:
    case 53:
    case 55: return "Drizzle";
    case 56:
    case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66:
    case 67: return "Freezing rain";
    case 71:
    case 73:
    case 75:
    case 77: return "Snow";
    case 80:
    case 81: return "Showers";
    case 82: return "Heavy showers";
    case 85:
    case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96:
    case 99: return "Thunderstorm, hail";
    default: return "Unknown";
    }
}
