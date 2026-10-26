#pragma once

#include <stddef.h>
#include "esp_err.h"

/*
 * Small HTTPS GET helper. One http_json_t per data source keeps its connection
 * alive between polls (TLS handshakes are slow on this board). Certificates are
 * checked against the ESP-IDF CA bundle; never disabled.
 */

typedef struct http_json http_json_t;

http_json_t *http_json_new(size_t max_body);

/* Send this header with every request (one only, e.g. an API key). Both
 * strings must outlive h: string literals or the generated secrets. */
void http_json_set_header(http_json_t *h, const char *name, const char *value);

/*
 * GET url. On ESP_OK, *body points to a NUL-terminated buffer owned by h
 * (valid until the next call) and *status holds the HTTP status, which may
 * still be an error status. Transport failures return the esp_err_t.
 */
esp_err_t http_json_get(http_json_t *h, const char *url, const char **body, size_t *len, int *status);
