/**
    mprTime.c - Date and Time handling
 *
    Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

/********************************** Defines ***********************************/

#if WINCE
    #define HAS_STRFTIME 0
#else
    #define HAS_STRFTIME 1
#endif

/*
    Token types ored inot the TimeToken value
 */
#define TOKEN_DAY       0x01000000
#define TOKEN_MONTH     0x02000000
#define TOKEN_ZONE      0x04000000
#define TOKEN_OFFSET    0x08000000
#define TOKEN_MASK      0xFF000000

typedef struct TimeToken {
    char    *name;
    int     value;
} TimeToken;

static TimeToken days[] = {
    { "sun",  0 | TOKEN_DAY },
    { "mon",  1 | TOKEN_DAY },
    { "tue",  2 | TOKEN_DAY },
    { "wed",  3 | TOKEN_DAY },
    { "thu",  4 | TOKEN_DAY },
    { "fri",  5 | TOKEN_DAY },
    { "sat",  6 | TOKEN_DAY },
    { 0, 0 },
};

static TimeToken fullDays[] = {
    { "sunday",     0 | TOKEN_DAY },
    { "monday",     1 | TOKEN_DAY },
    { "tuesday",    2 | TOKEN_DAY },
    { "wednesday",  3 | TOKEN_DAY },
    { "thursday",   4 | TOKEN_DAY },
    { "friday",     5 | TOKEN_DAY },
    { "saturday",   6 | TOKEN_DAY },
    { 0, 0 },
};

/*
    Make origin 1 to correspond to user date entries 10/28/2010
 */
static TimeToken months[] = {
    { "jan",  1 | TOKEN_MONTH },
    { "feb",  2 | TOKEN_MONTH },
    { "mar",  3 | TOKEN_MONTH },
    { "apr",  4 | TOKEN_MONTH },
    { "may",  5 | TOKEN_MONTH },
    { "jun",  6 | TOKEN_MONTH },
    { "jul",  7 | TOKEN_MONTH },
    { "aug",  8 | TOKEN_MONTH },
    { "sep",  9 | TOKEN_MONTH },
    { "oct", 10 | TOKEN_MONTH },
    { "nov", 11 | TOKEN_MONTH },
    { "dec", 12 | TOKEN_MONTH },
    { 0, 0 },
};

static TimeToken fullMonths[] = {
    { "january",    1 | TOKEN_MONTH },
    { "february",   2 | TOKEN_MONTH },
    { "march",      3 | TOKEN_MONTH },
    { "april",      4 | TOKEN_MONTH },
    { "may",        5 | TOKEN_MONTH },
    { "june",       6 | TOKEN_MONTH },
    { "july",       7 | TOKEN_MONTH },
    { "august",     8 | TOKEN_MONTH },
    { "september",  9 | TOKEN_MONTH },
    { "october",   10 | TOKEN_MONTH },
    { "november",  11 | TOKEN_MONTH },
    { "december",  12 | TOKEN_MONTH },
    { 0, 0 }
};

static TimeToken ampm[] = {
    { "am", 0 | TOKEN_OFFSET },
    { "pm", (12 * 3600) | TOKEN_OFFSET },
    { 0, 0 },
};


static TimeToken zones[] = {
    { "ut",      0 | TOKEN_ZONE},
    { "utc",     0 | TOKEN_ZONE},
    { "gmt",     0 | TOKEN_ZONE},
    { "edt",  -240 | TOKEN_ZONE},
    { "est",  -300 | TOKEN_ZONE},
    { "cdt",  -300 | TOKEN_ZONE},
    { "cst",  -360 | TOKEN_ZONE},
    { "mdt",  -360 | TOKEN_ZONE},
    { "mst",  -420 | TOKEN_ZONE},
    { "pdt",  -420 | TOKEN_ZONE},
    { "pst",  -480 | TOKEN_ZONE},
    { 0, 0 },
};


static TimeToken offsets[] = {
    { "tomorrow",    86400 | TOKEN_OFFSET},
    { "yesterday",  -86400 | TOKEN_OFFSET},
    { "next week",   (86400 * 7) | TOKEN_OFFSET},
    { "last week",  -(86400 * 7) | TOKEN_OFFSET},
    { 0, 0 },
};

static int timeSep = ':';

#if !HAS_STRFTIME
static char *abbrevDay[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};

static char *day[] = {
    "Sunday", "Monday", "Tuesday", "Wednesday",
    "Thursday", "Friday", "Saturday"
};

static char *abbrevMonth[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

static char *month[] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

static char *getTimeZoneName(MprCtx ctx, struct tm *tp);
#endif


#if BLD_WIN_LIKE || VXWORKS
static int gettimeofday(struct timeval *tv, struct timezone *tz);
#endif

#if BLD_UNIX_LIKE
#undef localtime
#undef localtime_r
#undef gmtime
#undef gmtime_r
#endif

static void validateTime(MprCtx ctx, struct tm *tm, struct tm *defaults);

/************************************ Code ************************************/
/*
    Initialize the time service
 */
int mprCreateTimeService(MprCtx ctx)
{
    Mpr                 *mpr;
    TimeToken           *tt;
    struct timezone     tz;
    struct timeval      tv;

    mpr = mprGetMpr(ctx);
    mpr->timeTokens = mprCreateHash(mpr, -1);
    ctx = mpr->timeTokens;

    for (tt = days; tt->name; tt++) {
        mprAddHash(mpr->timeTokens, tt->name, (void*) tt);
    }
    for (tt = fullDays; tt->name; tt++) {
        mprAddHash(mpr->timeTokens, tt->name, (void*) tt);
    }
    for (tt = months; tt->name; tt++) {
        mprAddHash(mpr->timeTokens, tt->name, (void*) tt);
    }
    for (tt = fullMonths; tt->name; tt++) {
        mprAddHash(mpr->timeTokens, tt->name, (void*) tt);
    }
    for (tt = ampm; tt->name; tt++) {
        mprAddHash(mpr->timeTokens, tt->name, (void*) tt);
    }
    for (tt = zones; tt->name; tt++) {
        mprAddHash(mpr->timeTokens, tt->name, (void*) tt);
    }
    for (tt = offsets; tt->name; tt++) {
        mprAddHash(mpr->timeTokens, tt->name, (void*) tt);
    }

    gettimeofday(&tv, &tz);
    mpr->timezone = -tz.tz_minuteswest;
    return 0;
}


/*
    Returns time in milliseconds since the epoch: 0:0:0 UTC Jan 1 1970.
 */
MprTime mprGetTime(MprCtx ctx)
{
#if VXWORKS
    struct timespec  tv;
    clock_gettime(CLOCK_REALTIME, &tv);
    return (MprTime) (((MprTime) tv.tv_sec) * 1000) + (tv.tv_nsec / (1000 * 1000));
#else
    struct timeval  tv;
    gettimeofday(&tv, NULL);
    return (MprTime) (((MprTime) tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
#endif
}


/*
    Return the number of milliseconds until the given timeout has expired.
 */
MprTime mprGetRemainingTime(MprCtx ctx, MprTime mark, uint timeout)
{
    MprTime     now, diff;

    now = mprGetTime(ctx);
    diff = (now - mark);

    if (diff < 0) {
        /*
            Detect time going backwards
         */
        diff = 0;
    }
    return (timeout - diff);
}


/*
    Return the elapsed time since a time marker
 */
MprTime mprGetElapsedTime(MprCtx ctx, MprTime mark)
{
    return mprGetTime(ctx) - mark;
}


int mprCompareTime(MprTime t1, MprTime t2)
{
    if (t1 < t2) {
        return -1;
    } else if (t1 == t2) {
        return 0;
    }
    return 1;
}


/*
    Make a time value interpreted using the local time value
 */
MprTime mprMakeTime(MprCtx ctx, struct tm *tm)
{
    MprTime     rc;
    
    tm->tm_isdst = -1;
    rc = mktime(tm);
    if (rc == -1) {
        return rc;
    }
    return rc * MPR_TICKS_PER_SEC;
}


/*
    Make a time value interpreted using UTC
 */
MprTime mprMakeUniversalTime(MprCtx ctx, struct tm *tm)
{
    MprTime     rc;
    
#if WIN
    rc = _mkgmtime(tm);
#elif VXWORKS
{
    struct timezone tz;
    struct timeval  tv;

    rc = mktime(tm);
    if (rc == -1) {
        return rc;
    }
    gettimeofday(&tv, &tz);
    rc -= (tz.tz_minuteswest * 60);
}
#else
    rc = timegm(tm);
#endif
    if (rc < 0) {
        return rc;
    }
    return rc * MPR_TICKS_PER_SEC;
}


struct tm *mprDecodeLocalTime(MprCtx ctx, struct tm *timep, MprTime time)
{
    time_t      when;

    when = (time_t) (time / MPR_TICKS_PER_SEC);
#if BLD_UNIX_LIKE || WINCE
    localtime_r(&when, timep);
    return timep;
#else
    *timep = *localtime(&when);
#endif
    return timep;
}


struct tm *mprDecodeUniversalTime(MprCtx ctx, struct tm *timep, MprTime time)
{
    time_t      when;

    when = (time_t) (time / MPR_TICKS_PER_SEC);
#if BLD_UNIX_LIKE || WINCE
    gmtime_r(&when, timep);
#else
    *timep = *gmtime(&when);
#endif
    return timep;
}


char *mprFormatLocalTime(MprCtx ctx, MprTime time)
{
    struct tm   tm;
    mprDecodeLocalTime(ctx, &tm, time);
    return mprFormatTime(ctx, MPR_DEFAULT_DATE, &tm);
}


/*
    Windows should now support C, e, k, P, s, 
 */
/*
    Format a time string. This uses strftime if available and so the supported formats vary from platform to platform.
    Strftime should supports some of these these formats:

         %A      full weekday name (Monday)
         %a      abbreviated weekday name (Mon)
         %B      full month name (January)
         %b      abbreviated month name (Jan)
W        %C      century. Year / 100. (0-N)
         %c      standard date and time representation
         %D      date (%m/%d/%y)
         %d      day-of-month (01-31)
I        %E*     POSIX locale extensions. The sequences %Ec %EC %Ex %EX %Ey %EY are supposed to provide alternate 
                 representations. 
W        %e      day-of-month with a leading space if only one digit ( 1-31)
I        %G      a year as a decimal number with century. This year is the one that contains the greater part of
                 the week (Monday as the first day of the week).
I        %g      the same year as in ``%G'', but as a decimal number without century (00-99).
         %H      hour (24 hour clock) (00-23)
I        %h      same as %b
         %I      hour (12 hour clock) (01-12)
         %j      day-of-year (001-366)
W        %k      hour (24 hour clock) (0-23)
         %l      the hour (12-hour clock) as a decimal number (1-12); single digits are preceded by a blank.
         %M      minute (00-59)
         %m      month (01-12)
         %n      a newline
I        %O*     POSIX locale extensions. The sequences %Od %Oe %OH %OI %Om %OM %OS %Ou %OU %OV %Ow %OW %Oy are 
                 supposed to provide alternate representations. Additionly %OB implemented to represent alternative 
                 months names (used standalone, without day mentioned). NOTE: these are not available on some platforms.
WM       %P      lower case am / pm
         %p      AM / PM
         %S      second (00-59)
W        %s      seconds since epoch
         %T      time (%H:%M:%S)
         %t      a tab.
         %U      week-of-year, first day sunday (00-53)
         %u      the weekday (Monday as the first day of the week) as a decimal number (1-7).
         %V      the week number of the year (Monday as the first day of the week) as a decimal number (01-53). If the week 
                 containing January 1 has four or more days in the new year, then it is week 1; otherwise it is the last 
                 week of the previous year, and the next week is week 1.
         %v      is equivalent to ``%e-%b-%Y''.
         %W      week-of-year, first day monday (00-53)
         %w      weekday (0-6, sunday is 0)
         %X      standard time representation
         %x      standard date representation
         %Y      year with century
         %y      year without century (00-99)
         %Z      timezone name
         %z      offset from UTC (-hhmm or +hhmm)
         %+      national representation of the date and time (the format is similar to that produced by date(1)).
         %%      percent sign

    The !HAS_STRFTIME version does not support: %E, %G, %g, %h, %O
    Windows only supports the following formats: A, a, B, b, c, d, H, I, j, M, m, p, S, U, W, w, X, x, Y, y, Z.
        i.e. windows does not support (C, e, k, P, s)

    Useful formats:
        RFC822: "%a, %d %b %Y %H:%M:%S %Z           "Fri, 07 Jan 2003 12:12:21 PDT"
                "%T %F                              "12:12:21 2007-01-03"
                "%v                                 "07-Jul-2003"
 */

#if WIN
    #define VALID_FMT "AaBbCcDdEeFGgHhIjklMmnOPpRrSsTtUuVvWwXxYyZz+%"
#elif MACOSX
    #define VALID_FMT "AaBbCcDdEeFGgHhIjklMmnOPpRrSsTtUuVvWwXxYyZz+%"
#else
    #define VALID_FMT "AaBbCcDdEeFGgHhIjklMmnOPpRrSsTtUuVvWwXxYyZz+%"
#endif

#if HAS_STRFTIME
/*
    Preferred implementation as strftime() will be localized
 */
char *mprFormatTime(MprCtx ctx, cchar *fmt, struct tm *tp)
{
    struct tm       tm;
    char            buf[MPR_MAX_STRING];
#if BLD_WIN_LIKE
    char            localFmt[MPR_MAX_STRING];
#endif

    if (fmt == 0) {
        fmt = MPR_DEFAULT_DATE;
    }
    if (tp == 0) {
        mprDecodeLocalTime(ctx, &tm, mprGetTime(ctx));
        tp = &tm;
    }
#if BLD_WIN_LIKE
{
    cchar   *cp, *pat;
    char    tz[80], *sign, *dp, *endp;
    long    timezone;
    int     size;

    /*
        Simulate: D, T, z
     */
    dp = localFmt;
    endp = &localFmt[sizeof(localFmt) - 1];
    for (cp = fmt, size = sizeof(localFmt) - 1; *cp && dp < &localFmt[sizeof(localFmt) - 32]; size = dp - endp - 1) {
        if (*cp == '%') {
            *dp++ = *cp++;
            if (*cp == '+') {
                pat = "a %b %d %H:%M:%S %Z %Y";
                strcpy(dp, pat);
                dp += strlen(pat);
                cp++;

            } else if (*cp == 'C') {
                mprItoa(dp, size, (int64) (1900 + tp->tm_year) / 1000, 10);
                dp += strlen(dp);
                cp++;

            } else if (*cp == 'D') {
                strcpy(dp, "m/%d/%y");
                dp += 7;
                cp++;

            } else if (*cp == 'e') {
                if (tp->tm_mon < 10) {
                    *dp++ = ' ';
                }
                mprItoa(dp, size - 1, (int64) tp->tm_mon);
                dp += strlen(dp);
                cp++;

            } else if (*cp == 'k') {
                if (tp->tm_hour < 10) {
                    *dp++ = ' ';
                }
                mprItoa(dp, size - 1, (int64) tp->tm_hour);
                dp += strlen(dp);
                cp++;

            } else if (*cp == 'P') {
                strcpy(dp, (tp->tm_hour > 11) ? "pm" : "am");
                dp += 2;
                cp++;

            } else if (*cp == 's') {
                mprItoa(dp, size, (int64) mprMakeTime(ctx, tp));
                dp += strlen(dp);
                cp++;

            } else if (*cp == 'T') {
                strcpy(dp, "H:%M:%S");
                dp += 7;
                cp++;

            } else if (*cp == 'z') {
                _get_timezone(&timezone);
                sign = (timezone >= 0) ? "-": "";
                if (timezone < 0) {
                    timezone = -timezone;
                }
                timezone /= 60;
                mprSprintf(tz, sizeof(tz), "%s%02d%02d", sign, timezone / 60, timezone % 60);
                len = strlen(tz);
                if (&dp[len] >= &localFmt[sizeof(localFmt) - 9]) {
                    break;
                }
                mprStrcpy(--dp, len + 1, tz);
                dp += len;
                cp++;

            } else if (strchr("AaBbCcDdEeFGgHhIjklMmnOPpRrSsTtUuVvWwXxYyZz+%", (int) *cp) != 0) {
                *dp++ = *cp++;
            }
        } else {
            *dp++ = *cp++;
        }
    }
    *dp = '\0';
    fmt = localFmt;
}
#endif
#if LINUX
    if (strcmp(fmt, "%+") == 0) {
        fmt = "%a %b %e %H:%M:%S %Z %Y";
    } else if (strcmp(fmt, "%v") == 0) {
         fmt = "%e-%b-%Y";
    }
#endif
    if (strftime(buf, sizeof(buf) - 1, fmt, tp) > 0) {
        buf[sizeof(buf) - 1] = '\0';
        return mprStrdup(ctx, buf);
    }
    return 0;
}


#else /* !HAS_STRFTIME */
/*
    This implementation is used only on platforms that don't support strftime. This version is not localized.
 */
static void digits(MprBuf *buf, int count, int value)
{
    char    tmp[16]; 
    int     i; 

    for (i = 0; count-- > 0; i++) { 
        tmp[i] = '0' + value % 10; 
        value /= 10; 
    } 
    while (i-- > 0) {
        mprPutCharToBuf(buf, tmp[i]); 
    } 
}


static char *getTimeZoneName(MprCtx ctx, struct tm *tp)
{
#if BLD_WIN_LIKE
    WCHAR                   *wzone;
    TIME_ZONE_INFORMATION   tz;
    GetTimeZoneInformation(&tz);
    wzone = tp->tm_isdst ? tz.DaylightName : tz.StandardName;
    return mprToAsc(ctx, wzone);
#else
    tzset();
    return mprStrdup(ctx, tzname);
#endif
}


char *mprFormatTime(MprCtx ctx, cchar *fmt, struct tm *tp)
{
    struct tm       tm;
    MprBuf          *buf;
    char            *result, *zone;
    int             w;

    if (fmt == 0) {
        fmt = MPR_DEFAULT_DATE;
    }
    if (tp == 0) {
        mprDecodeLocalTime(ctx, &tm, mprGetTime(ctx));
        tp = &tm;
    }
    if ((buf = mprCreateBuf(ctx, 64, -1)) == 0) {
        return 0;
    }
    while ((*fmt != '\0')) {
        if (*fmt++ != '%') {
            mprPutCharToBuf(buf, fmt[-1]);
            continue;
        }
        switch (*fmt++) {
        case '%' :                                      /* percent */
            mprPutCharToBuf(buf, '%');
            break;

        case '+' :                                      /* date (Mon May 18 23:29:50 PDT 2010) */
            mprPutStringToBuf(buf, abbrevDay[tp->tm_wday]);
            mprPutCharToBuf(buf, ' ');
            mprPutStringToBuf(buf, abbrevMonth[tp->tm_mon]);
            mprPutCharToBuf(buf, ' ');
            digits(buf, 2, tp->tm_mday);
            mprPutCharToBuf(buf, ' ');
            digits(buf, 2, tp->tm_hour);
            mprPutCharToBuf(buf, ':');
            digits(buf, 2, tp->tm_min);
            mprPutCharToBuf(buf, ':');
            digits(buf, 2, tp->tm_sec);
            mprPutCharToBuf(buf, ' ');
            zone = getTimeZoneName(ctx, tp);
            mprPutStringToBuf(buf, zone);
            mprPutCharToBuf(buf, ' ');
            digits(buf, 4, tp->tm_year + 1900);
            mprFree(zone);
            break;

        case 'a' :                                      /* abbreviated weekday (Sun) */
            mprPutStringToBuf(buf, abbrevDay[tp->tm_wday]);
            break;

        case 'A' :                                      /* full weekday (Sunday) */
            mprPutStringToBuf(buf, day[tp->tm_wday]);
            break;

        case 'b' :                                      /* abbreviated month (Jan) */
            mprPutStringToBuf(buf, abbrevMonth[tp->tm_mon]);
            break;

        case 'B' :                                      /* full month (January) */
            mprPutStringToBuf(buf, month[tp->tm_mon]);
            break;

        case 'c' :                                      /* preferred date+time in current locale */
            mprPutStringToBuf(buf, abbrevDay[tp->tm_wday]);
            mprPutCharToBuf(buf, ' ');
            mprPutStringToBuf(buf, abbrevMonth[tp->tm_mon]);
            mprPutCharToBuf(buf, ' ');
            digits(buf, 2, tp->tm_mday);
            mprPutCharToBuf(buf, ' ');
            digits(buf, 2, tp->tm_hour);
            mprPutCharToBuf(buf, ':');
            digits(buf, 2, tp->tm_min);
            mprPutCharToBuf(buf, ':');
            digits(buf, 2, tp->tm_sec);
            mprPutCharToBuf(buf, ' ');
            digits(buf, 2, tp->tm_year + 1900);
            break;

        case 'C' :                                      /* century number (19, 20) */
            digits(buf, 2, (1900 + tp->tm_year) / 1000);

        case 'd' :                                      /* day of month (01-31) */
            digits(buf, 2, tp->tm_mday);
            break;

        case 'D' :                                      /* mm/dd/yy */
            digits(buf, 2, tp->tm_mon + 1);
            mprPutCharToBuf(buf, '/');
            digits(buf, 2, tp->tm_mday);
            mprPutCharToBuf(buf, '/');
            digits(buf, 2, tp->tm_year + 1900);

        case 'H' :                                      /* hour using 24 hour clock (00-23) */
            digits(buf, 2, tp->tm_hour);
            break;

        case 'I' :                                      /* hour using 12 hour clock (00-01) */
            digits(buf, 2, (tp->tm_hour % 12) ? tp->tm_hour % 12 : 12);
            break;

        case 'j' :                                      /* julian day (001-366) */
            digits(buf, 3, tp->tm_yday+1);
            break;

        case 'm' :                                      /* month as a number (01-12) */
            digits(buf, 2, tp->tm_mon+1);
            break;

        case 'M' :                                      /* minute as a number (00-59) */
            digits(buf, 2, tp->tm_min);
            break;

        case 'p' :                                      /* AM/PM */
            mprPutStringToBuf(buf, (tp->tm_hour > 11) ? "PM" : "AM");
            break;

        case 'P' :                                      /* am/pm */
            mprPutStringToBuf(buf, (tp->tm_hour > 11) ? "pm" : "am");
            break;

        case 's' :                                      /* seconds since epoch */
            mprPutFmtToBuf(buf, "%d", mprMakeTime(ctx, tp));
            break;

        case 'S' :                                      /* seconds as a number (00-60) */
            digits(buf, 10, tp->tm_sec);
            break;

        case 'U' :                                      /* week number (00-53. Staring with first Sunday */
            w = tp->tm_yday / 7;
            if (tp->tm_yday % 7 > tp->tm_wday) {
                w++;
            }
            digits(buf, 2, w);
            break;

        case 'w' :                                      /* day of week (0-6) */
            digits(buf, 1, tp->tm_wday);
            break;

        case 'W' :                                      /* week number (00-53). Staring with first Monday */
            w = (tp->tm_yday + 7 - (tp->tm_wday ?  (tp->tm_wday - 1) : (7 - 1))) / 7;
            digits(buf, 2, w);
            break;

        case 'x' :                                      /* preferred date without time */
            digits(buf, 2, tp->tm_mon + 1);
            mprPutCharToBuf(buf, '/');
            digits(buf, 2, tp->tm_mday);
            mprPutCharToBuf(buf, '/');
            digits(buf, 2, tp->tm_year + 1900);
            break;

        case 'X' :                                      /* preferred time without date */
            digits(buf, 2, tp->tm_hour);
            mprPutCharToBuf(buf, ':');
            digits(buf, 2, tp->tm_min);
            mprPutCharToBuf(buf, ':');
            digits(buf, 2, tp->tm_sec);
            break;

        case 'y' :                                      /* year without century (00-99) */
            digits(buf, 2, tp->tm_year % 100);
            break;

        case 'Y' :                                      /* year as a decimal including century (1900) */
            digits(buf, 4, tp->tm_year + 1900);
            break;

        case 'Z' :                                      /* Timze zone */
            zone = getTimeZoneName(ctx, tp);
            mprPutStringToBuf(buf, zone);
            mprFree(zone);
            break;

        default:
            mprPutCharToBuf(buf, '%');
            mprPutCharToBuf(buf, fmt[-1]);
        }
    }
    mprAddNullToBuf(buf);
    result = mprStealBuf(ctx, buf);
    mprFree(buf);
    return result;
}
#endif /* HAS_STRFTIME */


static int lookupSym(Mpr *mpr, cchar *token, int kind)
{
    TimeToken   *tt;

    if ((tt = (TimeToken*) mprLookupHash(mpr->timeTokens, token)) == 0) {
        return -1;
    }
    if (kind != (tt->value & TOKEN_MASK)) {
        return -1;
    }
    return tt->value & ~TOKEN_MASK;
}


static int getNum(Mpr *mpr, char **token, int sep)
{
    int     num;

    if (*token == 0) {
        return 0;
    }

    num = atoi(*token);
    *token = strchr(*token, sep);
    if (*token) {
        *token += 1;
    }
    return num;
}


static int getNumOrSym(Mpr *mpr, char **token, int sep, int kind, int *isAlpah)
{
    char    *cp;
    int     num;

    mprAssert(token && *token);

    if (*token == 0) {
        return 0;
    }
    if (isalpha((int) **token)) {
        *isAlpah = 1;
        cp = strchr(*token, sep);
        if (cp) {
            *cp++ = '\0';
        }
        num = lookupSym(mpr, *token, kind);
        *token = cp;
        return num;
    }
    num = atoi(*token);
    *token = strchr(*token, sep);
    if (*token) {
        *token += 1;
    }
    *isAlpah = 0;
    return num;
}


static bool allDigits(cchar *token)
{
    cchar   *cp;

    for (cp = token; *cp; cp++) {
        if (!isdigit((int) *cp)) {
            return 0;
        }
    }
    return 1;
} 


static void swapDayMonth(struct tm *tm)
{
    int     tmp;

    tmp = tm->tm_mday;
    tm->tm_mday = tm->tm_mon;
    tm->tm_mon = tmp;
}


/*
    Parse the a date/time string according to the given timezone and return the result in *time. Missing date items 
    may be provided via the defaults argument.
 */ 
int mprParseTime(MprCtx ctx, MprTime *time, cchar *dateString, int timezone, struct tm *defaults)
{
    Mpr             *mpr;
    TimeToken       *tt;
    struct tm       tm;
    char            *str, *next, *token, *cp, *sep;
    int64           value;
    int             kind, hour, min, negate, value1, value2, value3, alpha, alpha2, alpha3;
    int             dateSep, offset, zoneOffset, explicitZone;

    mpr = mprGetMpr(ctx);

    offset = 0;
    zoneOffset = 0;
    explicitZone = 0;
    sep = ", \t";
    cp = 0;
    next = 0;

    /*
        Set these mandatory values to -1 so we can tell if they are set to valid values
        WARNING: all the calculations use tm_year with origin 0, not 1900. It is fixed up below.
     */
    tm.tm_isdst = tm.tm_year = tm.tm_mon = tm.tm_mday = tm.tm_hour = tm.tm_sec = tm.tm_min = tm.tm_wday = -1;
    tm.tm_min = tm.tm_sec = tm.tm_yday = -1;

    /*
        Set to -1 to cause mktime will try to determine if DST is in effect
     */
    tm.tm_isdst = -1;

    str = mprStrdup(ctx, dateString);
    mprStrLower(str);

    /*
        Handle ISO dates: "2009-05-21t16:06:05.000z
     */
    if (strchr(str, ' ') == 0 && strchr(str, '-') && str[strlen(str) - 1] == 'z') {
        for (cp = str; *cp; cp++) {
            if (*cp == '-') {
                *cp = '/';
            } else if (*cp == 't' && cp > str && isdigit((uchar) cp[-1]) && isdigit((uchar) cp[1]) ) {
                *cp = ' ';
            }
        }
    }
    token = mprStrTok(str, sep, &next);

    while (token && *token) {
        if (allDigits(token)) {
            /*
                Parse either day of month or year. Priority to day of month. Format: <29> Jan <15> <2010>
             */ 
            value = mprAtoi(token, 10);
            if (value > 3000) {
                *time = value;
                mprFree(str);
                return 0;
            } else if (value > 32 || (tm.tm_mday >= 0 && tm.tm_year < 0)) {
                tm.tm_year = (int) value;
            } else if (tm.tm_mday < 0) {
                tm.tm_mday = (int) value;
            }

        } else if ((*token == '+') || (*token == '-') ||
                ((strncmp(token, "gmt", 3) == 0 || strncmp(token, "utc", 3) == 0) &&
                ((cp = strchr(&token[3], '+')) != 0 || (cp = strchr(&token[3], '-')) != 0))) {
            /*
                Timezone. Format: [GMT|UTC][+-]NN[:]NN
             */
            if (!isalpha((int) *token)) {
                cp = token;
            }
            negate = *cp == '-' ? -1 : 1;
            cp++;
            hour = getNum(mpr, &cp, timeSep);
            if (hour >= 100) {
                hour /= 100;
            }
            min = getNum(mpr, &cp, timeSep);
            zoneOffset = negate * (hour * 60 + min);
            explicitZone = 1;

        } else if (isalpha((int) *token)) {
            if ((tt = (TimeToken*) mprLookupHash(mpr->timeTokens, token)) != 0) {
                kind = tt->value & TOKEN_MASK;
                value = tt->value & ~TOKEN_MASK; 
                switch (kind) {

                case TOKEN_DAY:
                    tm.tm_wday = (int) value;
                    break;

                case TOKEN_MONTH:
                    tm.tm_mon = (int) value;
                    break;

                case TOKEN_OFFSET:
                    /* Named timezones or symbolic names like: tomorrow, yesterday, next week ... */ 
                    offset += (int) value;
                    break;

                case TOKEN_ZONE:
                    zoneOffset = (int) value;
                    explicitZone = 1;
                    break;

                default:
                    /* Just ignore unknown values */
                    break;
                }
            }

        } else if ((cp = strchr(token, timeSep)) != 0 && isdigit((int) token[0])) {
            /*
                Time:  10:52[:23]
                Must not parse GMT-07:30
             */
            tm.tm_hour = getNum(mpr, &token, timeSep);
            tm.tm_min = getNum(mpr, &token, timeSep);
            tm.tm_sec = getNum(mpr, &token, timeSep);

        } else {
            dateSep = '/';
            if (strchr(token, dateSep) == 0) {
                dateSep = '-';
                if (strchr(token, dateSep) == 0) {
                    dateSep = '.';
                    if (strchr(token, dateSep) == 0) {
                        dateSep = 0;
                    }
                }
            }
            if (dateSep) {
                /*
                    Date:  07/28/2010, 07/28/08, Jan/28/2010, Jaunuary-28-2010, 28-jan-2010
                    Support order: dd/mm/yy, mm/dd/yy and yyyy/mm/dd
                    Support separators "/", ".", "-"
                 */
                value1 = getNumOrSym(mpr, &token, dateSep, TOKEN_MONTH, &alpha);
                value2 = getNumOrSym(mpr, &token, dateSep, TOKEN_MONTH, &alpha2);
                value3 = getNumOrSym(mpr, &token, dateSep, TOKEN_MONTH, &alpha3);

                if (value1 > 31) {
                    /* yy/mm/dd */
                    tm.tm_year = value1;
                    tm.tm_mon = value2;
                    tm.tm_mday = value3;

                } else if (value1 > 12 || alpha2) {
                    /* 
                        dd/mm/yy 
                        Can't detect 01/02/03  This will be evaluated as Jan 2 2003 below.
                     */  
                    tm.tm_mday = value1;
                    tm.tm_mon = value2;
                    tm.tm_year = value3;

                } else {
                    /*
                        The default to parse is mm/dd/yy unless the mm value is out of range
                     */
                    tm.tm_mon = value1;
                    tm.tm_mday = value2;
                    tm.tm_year = value3;
                }
            }
        }
        token = mprStrTok(NULL, sep, &next);
    }
    mprFree(str);

    /*
        Y2K fix and rebias
     */
    if (0 <= tm.tm_year && tm.tm_year < 100) {
        if (tm.tm_year < 50) {
            tm.tm_year += 2000;
        } else {
            tm.tm_year += 1900;
        }
    }    
    if (tm.tm_year >= 1900) {
        tm.tm_year -= 1900;
    }

    /*
        Convert back to origin 0 for months
     */
    tm.tm_mon--;

    /*
        Validate and fill in missing items with defaults
     */
    validateTime(mpr, &tm, defaults);

    if (timezone == MPR_LOCAL_TIMEZONE && !explicitZone) {
        *time = mprMakeTime(ctx, &tm);
    } else {
        *time = mprMakeUniversalTime(ctx, &tm);
        *time += -(zoneOffset * 60 * MPR_TICKS_PER_SEC);
    }
    if (*time < 0) {
        return MPR_ERR_WONT_FIT;
    }
    *time += (offset * MPR_TICKS_PER_SEC);
    return 0;
}


static void validateTime(MprCtx ctx, struct tm *tm, struct tm *defaults)
{
    struct tm   empty;

    /*
        Fix apparent day-mon-year ordering issues. Can't fix everything!
     */
    if ((12 <= tm->tm_mon && tm->tm_mon <= 31) && 0 <= tm->tm_mday && tm->tm_mday <= 11) {
        /*
            Looks like day month are swapped
         */
        swapDayMonth(tm);
    }

    if (tm->tm_year >= 0 && tm->tm_mon >= 0 && tm->tm_mday >= 0 && tm->tm_hour >= 0) {
        /*  Everything defined */
        return;
    }

    /*
        Use empty time if missing
     */
    if (defaults == NULL) {
        memset(&empty, 0, sizeof(empty));
        defaults = &empty;
        empty.tm_mday = 1;
        empty.tm_year = 70;
    }

    if (tm->tm_hour < 0 && tm->tm_min < 0 && tm->tm_sec < 0) {
        tm->tm_hour = defaults->tm_hour;
        tm->tm_min = defaults->tm_min;
        tm->tm_sec = defaults->tm_sec;
    }

    /*
        Get weekday, if before today then make next week
     */
    if (tm->tm_wday >= 0 && tm->tm_year == 0 && tm->tm_mon < 0 && tm->tm_mday < 0) {
        tm->tm_mday = defaults->tm_mday + (tm->tm_wday - defaults->tm_wday + 7) % 7;
        tm->tm_mon = defaults->tm_mon;
        tm->tm_year = defaults->tm_year;
    }

    /*
        Get month, if before this month then make next year
     */
    if (tm->tm_mon >= 0 && tm->tm_mon <= 11 && tm->tm_mday < 0) {
        if (tm->tm_year < 0) {
            tm->tm_year = defaults->tm_year + (((tm->tm_mon - defaults->tm_mon) < 0) ? 1 : 0);
        }
        tm->tm_mday = defaults->tm_mday;
    }

    /*
        Get date, if before current time then make tomorrow
     */
    if (tm->tm_hour >= 0 && tm->tm_year < 0 && tm->tm_mon < 0 && tm->tm_mday < 0) {
        tm->tm_mday = defaults->tm_mday + ((tm->tm_hour - defaults->tm_hour) < 0 ? 1 : 0);
        tm->tm_mon = defaults->tm_mon;
        tm->tm_year = defaults->tm_year;
    }

    if (tm->tm_year < 0) {
        tm->tm_year = defaults->tm_year;
    }
    if (tm->tm_mon < 0) {
        tm->tm_mon = defaults->tm_mon;
    }
    if (tm->tm_mday < 0) {
        tm->tm_mday = defaults->tm_mday;
    }
    if (tm->tm_hour < 0) {
        tm->tm_hour = defaults->tm_hour;
    }
    if (tm->tm_min < 0) {
        tm->tm_min = defaults->tm_min;
    }
    if (tm->tm_sec < 0) {
        tm->tm_sec = defaults->tm_sec;
    }
}


/*
    Compatibility for windows and brew
 */
#if BLD_WIN_LIKE || VXWORKS
static int gettimeofday(struct timeval *tv, struct timezone *tz)
{
#if BLD_WIN_LIKE
    FILETIME        fileTime;
    MprTime         now;
    static int      tzOnce;

    if (NULL != tv) {
        /*
            Convert from 100-nanosec units to microsectonds
         */
        GetSystemTimeAsFileTime(&fileTime);
        now = ((((MprTime) fileTime.dwHighDateTime) << BITS(uint)) + ((MprTime) fileTime.dwLowDateTime));
        now /= 10;

        now -= TIME_GENESIS;
        tv->tv_sec = (long) (now / 1000000);
        tv->tv_usec = (long) (now % 1000000);
    }

    if (NULL != tz) {
        TIME_ZONE_INFORMATION   zone;
        int                     rc, bias;
        rc = GetTimeZoneInformation(&zone);
        bias = (int) zone.Bias;
        if (rc == TIME_ZONE_ID_DAYLIGHT) {
            tz->tz_dsttime = 1;
        } else {
            tz->tz_dsttime = 0;
        }
        tz->tz_minuteswest = bias;
    }

#elif VXWORKS
    struct tm       tm;
    struct timespec now;
    time_t          t;
    char            *tze, *p;
    int rc;

    if ((rc = clock_gettime(CLOCK_REALTIME, &now)) == 0) {
        tv->tv_sec  = now.tv_sec;
        tv->tv_usec = (now.tv_nsec + 500) / 1000;
        if ((tze = getenv("TIMEZONE")) != 0) {
            if ((p = strchr(tze, ':')) != 0) {
                if ((p = strchr(tze, ':')) != 0) {
                    tz->tz_minuteswest = mprAtoi(++p, 10);
                }
            }
            t = tickGet();
            tz->tz_dsttime = (localtime_r(&t, &tm) == 0) ? tm.tm_isdst : 0;
        }
    }
    return rc;
#endif
    return 0;
}
#endif


/*
    High resolution timer
 */
#if BLD_DEBUG && KEEP
    #if BLD_UNIX_LIKE
        #if MPR_CPU_IX86
            inline MprTime mprGetHiResTime() {
                MprTime  now;
                __asm__ __volatile__ ("rdtsc" : "=A" (now));
                return now;
            }
        #endif /* MPR_CPU_IX86 */

    #elif BLD_WIN_LIKE
        inline MprTime mprGetHiResTime()
        {
            MprTime  now;
            QueryPerformanceCounter((LARGE_INTEGER*) &now);
            return now;
        }
    #endif /* BLD_WIN_LIKE */
#endif /* BLD_DEBUG */


/*
    @copy   default

    Copyright (c) Embedthis Software LLC, 2003-2010. All Rights Reserved.
    Copyright (c) Michael O'Brien, 1993-2010. All Rights Reserved.

    This software is distributed under commercial and open source licenses.
    You may use the GPL open source license described below or you may acquire
    a commercial license from Embedthis Software. You agree to be fully bound
    by the terms of either license. Consult the LICENSE.TXT distributed with
    this software for full details.

    This software is open source; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation; either version 2 of the License, or (at your
    option) any later version. See the GNU General Public License for more
    details at: http://www.embedthis.com/downloads/gplLicense.html

    This program is distributed WITHOUT ANY WARRANTY; without even the
    implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

    This GPL license does NOT permit incorporating this software into
    proprietary programs. If you are unable to comply with the GPL, you must
    acquire a commercial license to use this software. Commercial licenses
    for this software and support services are available from Embedthis
    Software at http://www.embedthis.com

    @end
 */
