/*
 * sunriset.c — computes Sun rise/set times, start/end of twilight, and
 *               the length of the day at any date and latitude.
 *
 * (c) Paul Schlyter, 1989, 1992 — released to the public domain.
 * Adapted from the ESP8266 user/sunriset.c for ESP-IDF.
 */
#include <math.h>
#include <stdlib.h>
#include "sunriset.h"

int __sunriset__(int year, int month, int day, double lon, double lat,
                 double altit, int upper_limb, double *trise, double *tset)
{
    double d, sr, sRA, sdec, sradius, t, tsouth, sidtime;
    int rc = 0;

    d = DAYS_SINCE_2000_JAN_0(year, month, day) + 0.5 - lon / 360.0;
    sidtime = revolution(GMST0(d) + 180.0 + lon);
    sun_RA_dec(d, &sRA, &sdec, &sr);
    tsouth = 12.0 - rev180(sidtime - sRA) / 15.0;
    sradius = 0.2666 / sr;
    if (upper_limb)
        altit -= sradius;

    double cost = (sind(altit) - sind(lat) * sind(sdec)) /
                  (cosd(lat) * cosd(sdec));
    if (cost >= 1.0)
        rc = -1, t = 0.0;
    else if (cost <= -1.0)
        rc = +1, t = 12.0;
    else
        t = acosd(cost) / 15.0;

    *trise = tsouth - t;
    *tset  = tsouth + t;
    return rc;
}

double __daylen__(int year, int month, int day, double lon, double lat,
                  double altit, int upper_limb)
{
    double d, obl_ecl, sr, slon, sin_sdecl, cos_sdecl, sradius, t;

    d = DAYS_SINCE_2000_JAN_0(year, month, day) + 0.5 - lon / 360.0;
    obl_ecl = 23.4393 - 3.563E-7 * d;
    sunpos(d, &slon, &sr);
    sin_sdecl = sind(obl_ecl) * sind(slon);
    cos_sdecl = sqrt(1.0 - sin_sdecl * sin_sdecl);
    sradius = 0.2666 / sr;
    if (upper_limb)
        altit -= sradius;

    double cost = (sind(altit) - sind(lat) * sin_sdecl) /
                  (cosd(lat) * cos_sdecl);
    if (cost >= 1.0)
        t = 0.0;
    else if (cost <= -1.0)
        t = 24.0;
    else
        t = (2.0 / 15.0) * acosd(cost);
    return t;
}

void sunpos(double d, double *lon, double *r)
{
    double M, w, e, E, x, y, v;
    M = revolution(356.0470 + 0.9856002585 * d);
    w = 282.9404 + 4.70935E-5 * d;
    e = 0.016709 - 1.151E-9 * d;
    E = M + e * RADEG * sind(M) * (1.0 + e * cosd(M));
    x = cosd(E) - e;
    y = sqrt(1.0 - e * e) * sind(E);
    *r = sqrt(x * x + y * y);
    v = atan2d(y, x);
    *lon = v + w;
    if (*lon >= 360.0)
        *lon -= 360.0;
}

void sun_RA_dec(double d, double *RA, double *dec, double *r)
{
    double lon, obl_ecl, xs, ys, xe, ye, ze;
    sunpos(d, &lon, r);
    xs = *r * cosd(lon);
    ys = *r * sind(lon);
    obl_ecl = 23.4393 - 3.563E-7 * d;
    xe = xs;
    ye = ys * cosd(obl_ecl);
    ze = ys * sind(obl_ecl);
    *RA = atan2d(ye, xe);
    *dec = atan2d(ze, sqrt(xe * xe + ye * ye));
}

#define INV360 (1.0 / 360.0)

double revolution(double x)
{
    return x - 360.0 * floor(x * INV360);
}

double rev180(double x)
{
    return x - 360.0 * floor(x * INV360 + 0.5);
}

double GMST0(double d)
{
    return revolution((180.0 + 356.0470 + 282.9404) +
                      (0.9856002585 + 4.70935E-5) * d);
}