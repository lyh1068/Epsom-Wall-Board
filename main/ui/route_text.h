#pragma once

#include <stddef.h>

/*
 * Route wording shared by the radar detail panel and the Morning page's
 * overhead chip. Both call routes_lookup(), so they also queue the fetch.
 */

/* "LHR arrival", "LGW departure", "MRS > LHR", plus "(unverified)" where due. */
void route_short(const char *flight, char *buf, size_t size);

/* Codes line ("MRS > LHR  (unverified)") and names line ("Marseille to London  •  British Airways"). */
void route_long(const char *flight, char *codes, char *names, size_t size);
