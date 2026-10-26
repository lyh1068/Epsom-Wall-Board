#include "trains_parse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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

static bool get_bool(const cJSON *obj, const char *key)
{
    return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(obj, key));
}

/* "WAT" in "WAT,VIC,LBG"? */
static bool crs_in(const char *crs, const char *csv)
{
    size_t n = strlen(crs);
    if (n == 0) {
        return false;
    }
    for (const char *p = csv; *p;) {
        const char *end = strchr(p, ',');
        size_t len = end ? (size_t)(end - p) : strlen(p);
        if (len == n && strncmp(p, crs, n) == 0) {
            return true;
        }
        if (end == NULL) {
            break;
        }
        p = end + 1;
    }
    return false;
}

/* Destinations, " & "-joined: a train that divides has two. */
static void parse_dest(train_t *t, const cJSON *svc, const char *london_csv)
{
    const cJSON *d;
    t->dest[0] = '\0';
    cJSON_ArrayForEach(d, cJSON_GetObjectItemCaseSensitive(svc, "destination")) {
        char name[sizeof(t->dest)], crs[sizeof(t->dest_crs)];
        get_str(name, sizeof(name), d, "locationName");
        get_str(crs, sizeof(crs), d, "crs");
        if (t->dest[0] == '\0') {
            strcpy(t->dest_crs, crs);
            snprintf(t->dest, sizeof(t->dest), "%s", name);
        } else {
            size_t used = strlen(t->dest);
            snprintf(t->dest + used, sizeof(t->dest) - used, " & %s", name);
        }
        t->london = t->london || crs_in(crs, london_csv);
    }
}

/* Later stops: the first list is the train's own route (the others are the
 * portions it divides into), but any of them can make it London-bound. */
static void parse_calls(train_t *t, const cJSON *svc, const char *london_csv)
{
    const cJSON *list;
    bool first = true;
    cJSON_ArrayForEach(list, cJSON_GetObjectItemCaseSensitive(svc, "subsequentCallingPoints")) {
        const cJSON *cp;
        cJSON_ArrayForEach(cp, cJSON_GetObjectItemCaseSensitive(list, "callingPoint")) {
            char crs[4];
            get_str(crs, sizeof(crs), cp, "crs");
            t->london = t->london || crs_in(crs, london_csv);
            if (!first || t->ncalls >= TRAINS_CALLS_MAX) {
                continue;
            }
            train_call_t *c = &t->calls[t->ncalls++];
            get_str(c->name, sizeof(c->name), cp, "locationName");
            strcpy(c->crs, crs);
            get_str(c->st, sizeof(c->st), cp, "st");
            get_str(c->et, sizeof(c->et), cp, "et");
            if (get_bool(cp, "isCancelled")) {
                strcpy(c->et, "Cancelled");
            }
        }
        first = false;
    }
}

/* <p>, </p>, <br/>, <div>, <li>: tags that break a line, so stand for a space.
 * Inline ones (<b>, <a ...>) join their text to what is around it. */
static bool block_tag(const char *tag)
{
    static const char *const BLOCK[] = { "p", "br", "div", "li" };
    if (*tag == '/') {
        tag++;
    }
    size_t n = 0;
    while (isalpha((unsigned char)tag[n])) {
        n++;
    }
    for (size_t i = 0; i < sizeof(BLOCK) / sizeof(BLOCK[0]); i++) {
        if (strlen(BLOCK[i]) == n && strncasecmp(tag, BLOCK[i], n) == 0) {
            return true;
        }
    }
    return false;
}

/* Darwin's messages are HTML fragments: drop the tags, decode the common
 * entities and collapse whitespace, in place. */
static void strip_html(char *s)
{
    static const struct { const char *ent; char c; } ENT[] = {
        { "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' }, { "&quot;", '"' },
        { "&#39;", '\'' }, { "&apos;", '\'' }, { "&nbsp;", ' ' },
    };
    char *w = s;
    bool space = true;                      /* true: drops leading whitespace */
    for (const char *r = s; *r; r++) {
        char c = *r;
        if (c == '<') {
            const char *end = strchr(r, '>');
            if (end == NULL) {
                break;                      /* cut off mid-tag */
            }
            c = block_tag(r + 1) ? ' ' : '\0';
            r = end;
            if (c == '\0') {
                continue;
            }
        } else if (c == '&') {
            for (size_t i = 0; i < sizeof(ENT) / sizeof(ENT[0]); i++) {
                size_t n = strlen(ENT[i].ent);
                if (strncmp(r, ENT[i].ent, n) == 0) {
                    c = ENT[i].c;
                    r += n - 1;
                    break;
                }
            }
        }
        if (isspace((unsigned char)c)) {
            if (!space) {
                *w++ = ' ';
                space = true;
            }
            continue;
        }
        *w++ = c;
        space = false;
    }
    if (w > s && w[-1] == ' ') {
        w--;
    }
    *w = '\0';
}

/* Messages come as [{"Value": "<p>...</p>"}] (or bare strings): joined. */
static void parse_nrcc(train_board_t *out, const cJSON *root)
{
    const cJSON *m;
    out->nrcc[0] = '\0';
    cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(root, "nrccMessages")) {
        const cJSON *v = cJSON_IsString(m) ? m : cJSON_GetObjectItemCaseSensitive(m, "Value");
        if (!cJSON_IsString(v)) {
            v = cJSON_GetObjectItemCaseSensitive(m, "value");
        }
        if (!cJSON_IsString(v) || v->valuestring == NULL) {
            continue;
        }
        char *raw = strdup(v->valuestring);
        if (raw == NULL) {
            return;
        }
        strip_html(raw);
        char text[sizeof(out->nrcc)];
        text_fold_ascii(text, sizeof(text), raw);
        free(raw);
        if (text[0] == '\0') {
            continue;
        }
        size_t used = strlen(out->nrcc);
        snprintf(out->nrcc + used, sizeof(out->nrcc) - used, "%s%s", used ? "  /  " : "", text);
    }
}

bool trains_parse(const char *json, size_t len, const char *london_csv, train_board_t *out)
{
    memset(out, 0, sizeof(*out));
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root) || !cJSON_IsString(cJSON_GetObjectItemCaseSensitive(root, "crs"))) {
        cJSON_Delete(root);
        return false;
    }

    /* No trains (late at night, or a blockade) is null or absent, not an error. */
    const cJSON *svc;
    cJSON_ArrayForEach(svc, cJSON_GetObjectItemCaseSensitive(root, "trainServices")) {
        if (out->count >= TRAINS_MAX) {
            break;
        }
        train_t *t = &out->t[out->count++];
        get_str(t->std, sizeof(t->std), svc, "std");
        get_str(t->etd, sizeof(t->etd), svc, "etd");
        if (get_bool(svc, "isCancelled")) {
            strcpy(t->etd, "Cancelled");
        }
        get_str(t->platform, sizeof(t->platform), svc, "platform");
        get_str(t->operator_name, sizeof(t->operator_name), svc, "operator");
        const cJSON *len_item = cJSON_GetObjectItemCaseSensitive(svc, "length");
        t->coaches = cJSON_IsNumber(len_item) ? len_item->valueint : 0;
        parse_dest(t, svc, london_csv);
        parse_calls(t, svc, london_csv);
    }
    parse_nrcc(out, root);
    cJSON_Delete(root);
    return true;
}
