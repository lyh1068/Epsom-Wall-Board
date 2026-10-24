#pragma once

#include <stdbool.h>
#include <time.h>

/*
 * Positional astronomy for a sky chart: accurate to a few arcminutes for the
 * Sun and planets and ~0.3 degrees for the Moon, which is far below a pixel on
 * a 700 px all-sky disc. Pure C, host-tested against JPL Horizons.
 * All angles are degrees. "jd" is a Julian Date in UT.
 */

typedef enum {
    PLANET_MERCURY,
    PLANET_VENUS,
    PLANET_MARS,
    PLANET_JUPITER,
    PLANET_SATURN,
    PLANET_COUNT,
} planet_t;

typedef struct {
    double lat, lon;        /* observer, degrees north / east */
} observer_t;

double astro_jd(time_t utc);

/* Greenwich mean sidereal time, degrees 0..360. */
double astro_gmst(double jd);

/* J2000 mean equatorial -> mean equator and equinox of date (rigorous precession). */
void astro_precess(double ra2000, double dec2000, double jd, double *ra, double *dec);

/* Equatorial of date -> airless altitude and azimuth (from north, through east). */
void astro_horizontal(const observer_t *o, double jd, double ra, double dec, double *alt, double *az);

/* Apparent geocentric position of date. */
void astro_sun(double jd, double *ra, double *dec);

/* Geocentric position of date, plus horizontal parallax and phase: illum is the
 * lit fraction 0..1, and phase_angle is 0 at full, 180 at new. */
void astro_moon(double jd, double *ra, double *dec, double *parallax, double *illum, double *phase_angle);

/* Moon altitude/azimuth seen from the observer (parallax applied: up to ~1 degree). */
void astro_moon_horizontal(const observer_t *o, double jd, double *alt, double *az, double *illum, double *phase_angle);

/* Geocentric position of date. */
void astro_planet(planet_t p, double jd, double *ra, double *dec);

const char *astro_planet_name(planet_t p);

/* "Day", "Civil twilight", "Nautical twilight", "Astronomical twilight", "Night". */
const char *astro_sky_state(double sun_alt);

/*
 * The next time after `from` (searching up to 24 h) that the Sun's centre
 * crosses altitude `alt` going up (rising) or down; -0.833 is sunrise/sunset.
 * Returns 0 if it does not happen in that window.
 */
time_t astro_sun_crossing(const observer_t *o, time_t from, double alt, bool rising);
