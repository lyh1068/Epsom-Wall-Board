#include "tube.h"

#include <string.h>
#include "config.h"

#if TUBE_DEMO

bool tube_get(tube_status_t *out)
{
    static const tube_line_t DEMO[] = {
        { "Northern",        TUBE_GOOD,  "Good Service", "" },
        { "Waterloo & City", TUBE_GOOD,  "Good Service", "" },
        { "Jubilee",         TUBE_MINOR, "Minor Delays", "DEMO: sample reason text - live status arrives in Phase 7" },
        { "Circle",          TUBE_GOOD,  "Good Service", "" },
    };
    memset(out, 0, sizeof(*out));
    out->count = sizeof(DEMO) / sizeof(DEMO[0]);
    memcpy(out->line, DEMO, sizeof(DEMO));
    out->demo = true;
    return true;
}

#else
#error "Live tube status is Phase 7; set TUBE_DEMO to 1 until then"
#endif
