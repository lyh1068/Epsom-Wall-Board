#include "astro.h"

#include <math.h>

#define D2R (M_PI / 180.0)
#define R2D (180.0 / M_PI)
#define J2000 2451545.0
#define DELTA_T_DAYS (69.2 / 86400.0)   /* TT - UT, close enough for 2020s */

static double norm360(double a)
{
    a = fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

double astro_jd(time_t utc)
{
    return (double)utc / 86400.0 + 2440587.5;
}

double astro_gmst(double jd)
{
    double d = jd - J2000;
    double t = d / 36525.0;
    return norm360(280.46061837 + 360.98564736629 * d + 0.000387933 * t * t - t * t * t / 38710000.0);
}

void astro_precess(double ra0, double dec0, double jd, double *ra, double *dec)
{
    /* Meeus, Astronomical Algorithms ch. 21 (rigorous method, from J2000). */
    double t = (jd - J2000) / 36525.0;
    double zeta = (2306.2181 * t + 0.30188 * t * t + 0.017998 * t * t * t) / 3600.0;
    double z = (2306.2181 * t + 1.09468 * t * t + 0.018203 * t * t * t) / 3600.0;
    double theta = (2004.3109 * t - 0.42665 * t * t - 0.041833 * t * t * t) / 3600.0;
    double a = cos(dec0 * D2R) * sin((ra0 + zeta) * D2R);
    double b = cos(theta * D2R) * cos(dec0 * D2R) * cos((ra0 + zeta) * D2R) - sin(theta * D2R) * sin(dec0 * D2R);
    double c = sin(theta * D2R) * cos(dec0 * D2R) * cos((ra0 + zeta) * D2R) + cos(theta * D2R) * sin(dec0 * D2R);
    *ra = norm360(atan2(a, b) * R2D + z);
    *dec = asin(c) * R2D;
}

void astro_horizontal(const observer_t *o, double jd, double ra, double dec, double *alt, double *az)
{
    double ha = (astro_gmst(jd) + o->lon - ra) * D2R;
    double lat = o->lat * D2R, d = dec * D2R;
    double sin_alt = sin(lat) * sin(d) + cos(lat) * cos(d) * cos(ha);
    *alt = asin(sin_alt) * R2D;
    /* Azimuth from north, increasing east. */
    double y = -sin(ha) * cos(d);
    double x = sin(d) * cos(lat) - cos(d) * sin(lat) * cos(ha);
    *az = norm360(atan2(y, x) * R2D);
}

static double obliquity(double jd)
{
    return 23.439291 - 0.0130042 * (jd - J2000) / 36525.0;
}

static void ecl_to_eq(double lambda, double beta, double eps, double *ra, double *dec)
{
    double l = lambda * D2R, b = beta * D2R, e = eps * D2R;
    *ra = norm360(atan2(sin(l) * cos(e) - tan(b) * sin(e), cos(l)) * R2D);
    *dec = asin(sin(b) * cos(e) + cos(b) * sin(e) * sin(l)) * R2D;
}

void astro_sun(double jd, double *ra, double *dec)
{
    /* Astronomical Almanac low-precision formula, ~0.01 degrees. */
    double n = jd + DELTA_T_DAYS - J2000;
    double L = norm360(280.460 + 0.9856474 * n);
    double g = norm360(357.528 + 0.9856003 * n) * D2R;
    double lambda = L + 1.915 * sin(g) + 0.020 * sin(2 * g);
    ecl_to_eq(lambda, 0, 23.439 - 0.0000004 * n, ra, dec);
}

void astro_moon(double jd, double *ra, double *dec, double *parallax, double *illum, double *phase_angle)
{
    /* Astronomical Almanac low-precision series: ~0.3 deg in longitude. */
    double t = (jd + DELTA_T_DAYS - J2000) / 36525.0;
#define S(a, b) sin((a + b * t) * D2R)
#define C(a, b) cos((a + b * t) * D2R)
    double lambda = 218.32 + 481267.881 * t
                    + 6.29 * S(135.0, 477198.87) - 1.27 * S(259.3, -413335.36)
                    + 0.66 * S(235.7, 890534.22) + 0.21 * S(269.9, 954397.74)
                    - 0.19 * S(357.5, 35999.05) - 0.11 * S(186.5, 966404.03);
    double beta = 5.13 * S(93.3, 483202.02) + 0.28 * S(228.2, 960400.89)
                  - 0.28 * S(318.3, 6003.15) - 0.17 * S(217.6, -407332.21);
    double pi = 0.9508 + 0.0518 * C(135.0, 477198.87) + 0.0095 * C(259.3, -413335.36)
                + 0.0078 * C(235.7, 890534.22) + 0.0028 * C(269.9, 954397.74);
#undef S
#undef C
    lambda = norm360(lambda);
    ecl_to_eq(lambda, beta, obliquity(jd), ra, dec);
    *parallax = pi;

    /* Phase from the Sun-Moon elongation. */
    double sra, sdec;
    astro_sun(jd, &sra, &sdec);
    double cos_elong = sin(sdec * D2R) * sin(*dec * D2R) + cos(sdec * D2R) * cos(*dec * D2R) * cos((sra - *ra) * D2R);
    double elong = acos(fmax(-1.0, fmin(1.0, cos_elong)));
    /* The Moon is ~1/389 as far as the Sun, so the phase angle is almost 180 - elongation. */
    double i = atan2(sin(elong), (389.0 - cos(elong))) ;
    i = M_PI - elong - i;
    *phase_angle = i * R2D;
    *illum = (1 + cos(i)) / 2;
}

void astro_moon_horizontal(const observer_t *o, double jd, double *alt, double *az, double *illum, double *phase_angle)
{
    double ra, dec, pi;
    astro_moon(jd, &ra, &dec, &pi, illum, phase_angle);
    astro_horizontal(o, jd, ra, dec, alt, az);
    /* Topocentric: the Moon sits lower by about its parallax times cos(altitude). */
    *alt -= pi * cos(*alt * D2R);
}

/* JPL "Approximate Positions of the Planets", Table 1 (1800-2050): value at
 * J2000 and rate per Julian century for a, e, I, L, long. perihelion, long. node. */
static const double ELEMENTS[PLANET_COUNT + 1][12] = {
    { 0.38709927, 0.00000037, 0.20563593, 0.00001906, 7.00497902, -0.00594749,
      252.25032350, 149472.67411175, 77.45779628, 0.16047689, 48.33076593, -0.12534081 },
    { 0.72333566, 0.00000390, 0.00677672, -0.00004107, 3.39467605, -0.00078890,
      181.97909950, 58517.81538729, 131.60246718, 0.00268329, 76.67984255, -0.27769418 },
    { 1.52371034, 0.00001847, 0.09339410, 0.00007882, 1.84969142, -0.00813131,
      -4.55343205, 19140.30268499, -23.94362959, 0.44441088, 49.55953891, -0.29257343 },
    { 5.20288700, -0.00011607, 0.04838624, -0.00013253, 1.30439695, -0.00183714,
      34.39644051, 3034.74612775, 14.72847983, 0.21252668, 100.47390909, 0.20469106 },
    { 9.53667594, -0.00125060, 0.05386179, -0.00050991, 2.48599187, 0.00193609,
      49.95424423, 1222.49362201, 92.59887831, -0.41897216, 113.66242448, -0.28867794 },
    /* Earth-Moon barycentre */
    { 1.00000261, 0.00000562, 0.01671123, -0.00004392, -0.00001531, -0.01294668,
      100.46457166, 35999.37244981, 102.93768193, 0.32327364, 0.0, 0.0 },
};

/* Heliocentric ecliptic J2000 position in au. */
static void helio(int body, double t, double *x, double *y, double *z)
{
    const double *el = ELEMENTS[body];
    double a = el[0] + el[1] * t, e = el[2] + el[3] * t, I = (el[4] + el[5] * t) * D2R;
    double L = el[6] + el[7] * t, peri = el[8] + el[9] * t, node = el[10] + el[11] * t;
    double w = (peri - node) * D2R;
    double M = norm360(L - peri);
    if (M > 180) {
        M -= 360;
    }
    M *= D2R;
    double E = M + e * sin(M);
    for (int i = 0; i < 10; i++) {
        E -= (E - e * sin(E) - M) / (1 - e * cos(E));
    }
    double xp = a * (cos(E) - e), yp = a * sqrt(1 - e * e) * sin(E);
    double O = node * D2R;
    *x = (cos(w) * cos(O) - sin(w) * sin(O) * cos(I)) * xp + (-sin(w) * cos(O) - cos(w) * sin(O) * cos(I)) * yp;
    *y = (cos(w) * sin(O) + sin(w) * cos(O) * cos(I)) * xp + (-sin(w) * sin(O) + cos(w) * cos(O) * cos(I)) * yp;
    *z = (sin(w) * sin(I)) * xp + (cos(w) * sin(I)) * yp;
}

void astro_planet(planet_t p, double jd, double *ra, double *dec)
{
    double t = (jd + DELTA_T_DAYS - J2000) / 36525.0;
    double px, py, pz, ex, ey, ez;
    helio(PLANET_COUNT, t, &ex, &ey, &ez);
    helio(p, t, &px, &py, &pz);
    double x = px - ex, y = py - ey, z = pz - ez;
    /* Ecliptic J2000 -> equatorial J2000 -> of date. */
    double e0 = 23.43928 * D2R;
    double xq = x, yq = y * cos(e0) - z * sin(e0), zq = y * sin(e0) + z * cos(e0);
    double ra0 = norm360(atan2(yq, xq) * R2D);
    double dec0 = atan2(zq, sqrt(xq * xq + yq * yq)) * R2D;
    astro_precess(ra0, dec0, jd, ra, dec);
}

const char *astro_planet_name(planet_t p)
{
    static const char *const names[PLANET_COUNT] = { "Mercury", "Venus", "Mars", "Jupiter", "Saturn" };
    return p < PLANET_COUNT ? names[p] : "?";
}

const char *astro_sky_state(double sun_alt)
{
    if (sun_alt > -0.833) {
        return "Day";
    }
    if (sun_alt > -6) {
        return "Civil twilight";
    }
    if (sun_alt > -12) {
        return "Nautical twilight";
    }
    if (sun_alt > -18) {
        return "Astronomical twilight";
    }
    return "Night";
}

static double sun_alt_at(const observer_t *o, time_t t)
{
    double jd = astro_jd(t), ra, dec, alt, az;
    astro_sun(jd, &ra, &dec);
    astro_horizontal(o, jd, ra, dec, &alt, &az);
    return alt;
}

time_t astro_sun_crossing(const observer_t *o, time_t from, double alt, bool rising)
{
    /* 10-minute steps, then bisect to the second. */
    double prev = sun_alt_at(o, from) - alt;
    for (time_t t = from + 600; t <= from + 86400; t += 600) {
        double cur = sun_alt_at(o, t) - alt;
        if ((rising && prev < 0 && cur >= 0) || (!rising && prev > 0 && cur <= 0)) {
            time_t lo = t - 600, hi = t;
            while (hi - lo > 1) {
                time_t mid = lo + (hi - lo) / 2;
                double m = sun_alt_at(o, mid) - alt;
                if ((rising && m < 0) || (!rising && m > 0)) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            return hi;
        }
        prev = cur;
    }
    return 0;
}
