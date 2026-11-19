/*
 * sunriset.h — computes sunrise/sunset times, start/end of twilight,
 *               and the length of the day at any date and latitude.
 *
 * (c) Paul Schlyter, 1989, 1992 — public domain.
 */
#pragma once

#include <math.h>

/* A macro to compute the number of days elapsed since 2000 Jan 0.0
 * (which is equal to 1999 Dec 31, 0h UT) */
#define DAYS_SINCE_2000_JAN_0(y,m,d) \
    (367L*(y)-((7*((y)+(((m)+9)/12)))/4)+((275*(m))/9)+(d)-730530L)

#ifndef PI
 #define PI        3.1415926535897932384
#endif

#define RADEG     ( 180.0 / PI )
#define DEGRAD    ( PI / 180.0 )

#define sind(x)  sin((x)*DEGRAD)
#define cosd(x)  cos((x)*DEGRAD)
#define tand(x)  tan((x)*DEGRAD)

#define atand(x)    (RADEG*atan(x))
#define asind(x)    (RADEG*asin(x))
#define acosd(x)    (RADEG*acos(x))
#define atan2d(y,x) (RADEG*atan2(y,x))

/* Sunrise/set: Sun's upper limb 35 arc min below horizon */
#define sun_rise_set(year,month,day,lon,lat,rise,set) \
    __sunriset__( year, month, day, lon, lat, -35.0/60.0, 1, rise, set )

/* Civil twilight: Sun's center 6 degrees below horizon */
#define civil_twilight(year,month,day,lon,lat,start,end) \
    __sunriset__( year, month, day, lon, lat, -6.0, 0, start, end )

/* Nautical twilight: Sun's center 12 degrees below horizon */
#define nautical_twilight(year,month,day,lon,lat,start,end) \
    __sunriset__( year, month, day, lon, lat, -12.0, 0, start, end )

/* Astronomical twilight: Sun's center 18 degrees below horizon */
#define astronomical_twilight(year,month,day,lon,lat,start,end) \
    __sunriset__( year, month, day, lon, lat, -18.0, 0, start, end )

/* Day length with Sun's upper limb 50 arc min below horizon */
#define day_length(year,month,day,lon,lat) \
    __daylen__( year, month, day, lon, lat, -50.0/60.0, 1 )

double __daylen__(int year, int month, int day, double lon, double lat,
                  double altit, int upper_limb);

int __sunriset__(int year, int month, int day, double lon, double lat,
                 double altit, int upper_limb, double *rise, double *set);

void sunpos(double d, double *lon, double *r);
void sun_RA_dec(double d, double *RA, double *dec, double *r);
double revolution(double x);
double rev180(double x);
double GMST0(double d);