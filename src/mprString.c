/**
 *  mprString.c - String routines safe for embedded programming
 *
 *  This module provides safe replacements for the standard string library. 
 *
 *  Most routines in this file are not thread-safe. It is the callers responsibility to perform all thread 
 *  synchronization.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/************************************ Code ************************************/

int mprStrcpy(char *dest, int destMax, cchar *src)
{
    int     len;

    mprAssert(dest);
    mprAssert(destMax >= 0);
    mprAssert(src);
    mprAssert(src != dest);

    len = (int) strlen(src);
    if (destMax > 0 && len >= destMax && len > 0) {
        return MPR_ERR_WONT_FIT;
    }
    if (len > 0) {
        memcpy(dest, src, len);
        dest[len] = '\0';
    } else {
        *dest = '\0';
        len = 0;
    } 
    return len;
}


int mprStrcpyCount(char *dest, int destMax, cchar *src, int count)
{
    int     len;

    mprAssert(dest);
    mprAssert(destMax >= 0);
    mprAssert(src);
    mprAssert(src != dest);

    len = (int) strlen(src);
    len = min(len, count);

    if (destMax > 0 && len >= destMax && len > 0) {
        return MPR_ERR_WONT_FIT;
    }
    if (len > 0) {
        memcpy(dest, src, len);
        dest[len] = '\0';
    } else {
        *dest = '\0';
        len = 0;
    } 
    return len;
}


#if !BREW

int mprMemcmp(const void *s1, int s1Len, const void *s2, int s2Len)
{
    int     len, rc;

    mprAssert(s1);
    mprAssert(s2);
    mprAssert(s1Len >= 0);
    mprAssert(s2Len >= 0);

    len = min(s1Len, s2Len);

    rc = memcmp(s1, s2, len);
    if (rc == 0) {
        if (s1Len < s2Len) {
            return -1;
        } else if (s1Len > s2Len) {
            return 1;
        }
    }
    return rc;
}

#endif


/*
 *  Supports insitu copy where src and destination overlap
 */
int mprMemcpy(void *dest, int destMax, const void *src, int nbytes)
{
    mprAssert(dest);
    mprAssert(destMax <= 0 || destMax >= nbytes);
    mprAssert(src);
    mprAssert(nbytes >= 0);

    if (destMax > 0 && nbytes > destMax) {
        mprAssert(0);
        return MPR_ERR_WONT_FIT;
    }
    if (nbytes > 0) {
        memmove(dest, src, nbytes);
        return nbytes;
    } else {
        return 0;
    }
}


char *mprStrcatV(MprCtx ctx, int destMax, cchar *src, va_list args)
{
    va_list     ap;
    char        *dest, *str, *dp;
    int         required;

    mprAssert(ctx);
    mprAssert(src);

    if (destMax <= 0) {
        destMax = INT_MAX;
    }

#ifdef __va_copy
    __va_copy(ap, args);
#else
    ap = args;
#endif

    required = 1;
    str = (char*) src;

    while (str) {
        required += (int) strlen(str);
        str = va_arg(ap, char*);
    }
    if (required >= destMax) {
        return 0;
    }

    if ((dest = (char*) mprAlloc(ctx, required)) == 0) {
        return 0;
    }

    dp = dest;
#ifdef __va_copy
    __va_copy(ap, args);
#else
    ap = args;
#endif
    str = (char*) src;
    while (str) {
        strcpy(dp, str);
        dp += (int) strlen(str);
        str = va_arg(ap, char*);
    }
    *dp = '\0';
    return dest;
}


char *mprStrcat(MprCtx ctx, int destMax, cchar *src, ...)
{
    va_list     ap;
    char        *result;

    mprAssert(ctx);
    mprAssert(src);

    va_start(ap, src);
    result = mprStrcatV(ctx, destMax, src, ap);
    va_end(ap);
    return result;
}


char *mprReallocStrcat(MprCtx ctx, int destMax, char *buf, cchar *src, ...)
{
    va_list     ap;
    char        *str, *dp;
    int         required, existingLen;

    mprAssert(ctx);
    mprAssert(src);

    va_start(ap, src);
    if (destMax <= 0) {
        destMax = INT_MAX;
    }

    existingLen = (buf) ? (int) strlen(buf) : 0;
    required = existingLen + 1;

    str = (char*) src;
    while (str) {
        required += (int) strlen(str);
        str = va_arg(ap, char*);
    }
    if (required >= destMax) {
        return 0;
    }
    if ((buf = mprRealloc(ctx, (char*) buf, required)) == 0) {
        return 0;
    }
    dp = &buf[existingLen];

    va_end(ap);
    va_start(ap, src);

    str = (char*) src;
    while (str) {
        strcpy(dp, str);
        dp += (int) strlen(str);
        str = va_arg(ap, char*);
    }
    *dp = '\0';
    va_end(ap);
    return buf;
}


int mprStrlen(cchar *src, int max)
{
    int     len;

    len = (int) strlen(src);
    if (len >= max) {
        mprAssert(0);
        return MPR_ERR_WONT_FIT;
    }
    return len;
}


char *mprStrTrim(char *str, cchar *set)
{
    int     len, i;

    if (str == 0 || set == 0) {
        return str;
    }

    i = (int) strspn(str, set);
    str += i;

    len = (int) strlen(str);
    while (len > 0 && strspn(&str[len - 1], set) > 0) {
        str[len - 1] = '\0';
        len--;
    }
    return str;
}


/*  
 *  Map a string to lower case (overwrites original string)
 */
char *mprStrLower(char *str)
{
    char    *cp;

    mprAssert(str);

    if (str == 0) {
        return 0;
    }

    for (cp = str; *cp; cp++) {
        if (isupper((int) *cp)) {
            *cp = (char) tolower((int) *cp);
        }
    }
    return str;
}


/*  
 *  Map a string to upper case (overwrites buffer)
 */
char *mprStrUpper(char *str)
{
    char    *cp;

    mprAssert(str);
    if (str == 0) {
        return 0;
    }

    for (cp = str; *cp; cp++) {
        if (islower((int) *cp)) {
            *cp = (char) toupper((int) *cp);
        }
    }
    return str;
}


/*
 *  Case sensitive string comparison.
 */
int mprStrcmp(cchar *str1, cchar *str2)
{
    int     rc;

    if (str1 == 0) {
        return -1;
    }
    if (str2 == 0) {
        return 1;
    }
    if (str1 == str2) {
        return 0;
    }

    for (rc = 0; *str1 && *str2 && rc == 0; str1++, str2++) {
        rc = *str1 - *str2;
    }
    if (rc) {
        return rc < 0 ? -1 : 1;
    }
    if (*str1 == '\0' && *str2) {
        return -1;
    }
    if (*str2 == '\0' && *str1) {
        return 1;
    }
    return rc;
}


/*
 *  Case sensitive string comparison. Limited by length
 */
int mprStrcmpCount(cchar *str1, cchar *str2, int len)
{
    int     rc;

    if (str1 == 0 || str2 == 0) {
        return -1;
    }
    if (str1 == str2) {
        return 0;
    }

    for (rc = 0; len-- > 0 && *str1 && rc == 0; str1++, str2++) {
        rc = *str1 - *str2;
    }
    if (rc || len < 0) {
        return rc;
    } else if (*str1 == '\0' && *str2 == '\0') {
        return 0;
    } else if (*str1 == '\0') {
        return -1;
    } else if (*str2 == '\0') {
        return 1;
    }
    return 0;
}


int mprStrStartsWith(cchar *dest, cchar *pat)
{
    return mprStrcmpCount(dest, pat, (int) strlen(pat)) == 0;
}


/*
 *  Case insensitive string comparison. Stop at the end of str1.
 */
int mprStrcmpAnyCase(cchar *str1, cchar *str2)
{
    int     rc;

    if (str1 == 0) {
        return -1;
    }
    if (str2 == 0) {
        return 1;
    }
    if (str1 == str2) {
        return 0;
    }
    for (rc = 0; *str1 && *str2 && rc == 0; str1++, str2++) {
        rc = tolower((int) *str1) - tolower((int) *str2);
    }
    if (rc) {
        return rc < 0 ? -1 : 1;
    } else if (*str1 == '\0' && *str2 == '\0') {
        return 0;
    } else if (*str1 == '\0') {
        return -1;
    } else if (*str2 == '\0') {
        return 1;
    }

    return 0;
}


/*
 *  Case insensitive string comparison. Limited by length
 */
int mprStrcmpAnyCaseCount(cchar *str1, cchar *str2, int len)
{
    int     rc;

    if (str1 == 0 || str2 == 0) {
        return -1;
    }
    if (str1 == str2) {
        return 0;
    }

    for (rc = 0; len-- > 0 && *str1 && rc == 0; str1++, str2++) {
        rc = tolower((int) *str1) - tolower((int) *str2);
    }
    if (rc || len < 0) {
        return rc;
    } else if (*str1 == '\0' && *str2 == '\0') {
        return 0;
    } else if (*str1 == '\0') {
        return -1;
    } else if (*str2 == '\0') {
        return 1;
    }
    return 0;
}


/*
 *  Thread-safe wrapping of strtok. Note "str" is modifed as per strtok()
 */
char *mprStrTok(char *str, cchar *delim, char **last)
{
    char    *start, *end;
    int     i;

    start = str ? str : *last;

    if (start == 0) {
        *last = 0;
        return 0;
    }
    
    i = (int) strspn(start, delim);
    start += i;
    if (*start == '\0') {
        *last = 0;
        return 0;
    }
    end = strpbrk(start, delim);
    if (end) {
        *end++ = '\0';
        i = (int) strspn(end, delim);
        end += i;
    }
    *last = end;
    return start;
}


/*
 *  Split the buffer into word tokens
 */
char *mprGetWordTok(char *buf, int bufsize, cchar *str, cchar *delim, cchar **tok)
{
    cchar       *start, *end;
    int         i, len;

    start = str ? str : *tok;

    if (start == 0) {
        return 0;
    }
    
    i = (int) strspn(start, delim);
    start += i;
    if (*start =='\0') {
        *tok = 0;
        return 0;
    }
    end = strpbrk(start, delim);
    if (end) {
        len = min((int) (end - start), bufsize - 1);
        mprMemcpy(buf, bufsize, start, len);
        buf[len] = '\0';
    } else {
        if (mprStrcpy(buf, bufsize, start) < 0) {
            buf[bufsize - 1] = '\0';
            return 0;
        }
        buf[bufsize - 1] = '\0';
    }
    *tok = end;
    return buf;
}


/*
 *  Format a number as a string. Support radix 10 and 16.
 */
char *mprItoa(char *buf, int size, int64 value, int radix)
{
    char    numBuf[32];
    char    *cp, *dp, *endp;
    char    digits[] = "0123456789ABCDEF";
    int     negative;

    if (radix != 10 && radix != 16) {
        return 0;
    }

    cp = &numBuf[sizeof(numBuf)];
    *--cp = '\0';

    if (value < 0) {
        negative = 1;
        value = -value;
        size--;
    } else {
        negative = 0;
    }

    do {
        *--cp = digits[value % radix];
        value /= radix;
    } while (value > 0);

    if (negative) {
        *--cp = '-';
    }

    dp = buf;
    endp = &buf[size];
    while (dp < endp && *cp) {
        *dp++ = *cp++;
    }
    *dp++ = '\0';
    return buf;
}


/*
    Parse an ascii number. Supports radix 8, 10 or 16. If radix is <= 0, then the radix is sleuthed from the input.
    Supports formats:
        [(+|-)][0][OCTAL_DIGITS]
        [(+|-)][0][(x|X)][HEX_DIGITS]
        [(+|-)][DIGITS]

 */
int64 mprAtoi(cchar *str, int radix)
{
    int64   val;
    int     c, negative;

    mprAssert(radix <= 0 || radix == 8 || radix == 10 || radix == 16);

    if (str == 0) {
        return 0;
    }
    while (isspace((int) *str)) {
        str++;
    }
    val = 0;
    if (*str == '-') {
        negative = 1;
        str++;
    } else {
        negative = 0;
    }
    if (radix <= 0) {
        radix = 10;
        if (*str == '0') {
            if (tolower((int) str[1]) == 'x') {
                radix = 16;
                str += 2;
            } else {
                radix = 8;
                str++;
            }
        }
    } else if (radix == 16) {
        if (*str == '0' && tolower((int) str[1]) == 'x') {
            str += 2;
        }
    }

    if (radix == 16) {
        while (*str) {
            c = tolower((int) *str);
            if (isdigit(c)) {
                val = (val * radix) + c - '0';
            } else if (c >= 'a' && c <= 'f') {
                val = (val * radix) + c - 'a' + 10;
            } else {
                break;
            }
            str++;
        }
    } else {
        while (*str && isdigit((int) *str)) {
            val = (val * radix) + *str - '0';
            str++;
        }
    }
    return (negative) ? -val: val;
}


/*
 *  Make an argv array. Caller must free by calling mprFree(argv) to free everything.
 */
int mprMakeArgv(MprCtx ctx, cchar *program, cchar *cmd, int *argcp, char ***argvp)
{
    char        *cp, **argv, *buf, *args;
    int         size, argc;

    /*
     *  Allocate one buffer for argv and the actual args themselves
     */
    size = (int) strlen(cmd) + 1;

    buf = (char*) mprAlloc(ctx, (MPR_MAX_ARGC * (int) sizeof(char*)) + size);
    if (buf == 0) {
        return MPR_ERR_NO_MEMORY;
    }

    args = &buf[MPR_MAX_ARGC * sizeof(char*)];
    strcpy(args, cmd);
    argv = (char**) buf;

    argc = 0;
    if (program) {
        argv[argc++] = (char*) mprStrdup(ctx, program);
    }

    for (cp = args; cp && *cp != '\0'; argc++) {
        if (argc >= MPR_MAX_ARGC) {
            mprAssert(argc < MPR_MAX_ARGC);
            mprFree(buf);
            *argvp = 0;
            if (argcp) {
                *argcp = 0;
            }
            return MPR_ERR_TOO_MANY;
        }
        while (isspace((int) *cp)) {
            cp++;
        }
        if (*cp == '\0')  {
            break;
        }
        if (*cp == '"') {
            cp++;
            argv[argc] = cp;
            while ((*cp != '\0') && (*cp != '"')) {
                cp++;
            }
        } else if (*cp == '\'') {
            cp++;
            argv[argc] = cp;
            while ((*cp != '\0') && (*cp != '\'')) {
                cp++;
            }
        } else {
            argv[argc] = cp;
            while (*cp != '\0' && !isspace((int) *cp)) {
                cp++;
            }
        }
        if (*cp != '\0') {
            *cp++ = '\0';
        }
    }
    argv[argc] = 0;

    if (argcp) {
        *argcp = argc;
    }
    *argvp = argv;

    return argc;
}


char *mprStrnstr(cchar *str, cchar *pattern, int len)
{
    cchar   *start, *p;
    int     i;

    if (str == 0 || pattern == 0 || len == 0) {
        return 0;
    }

    while (*str && len-- > 0) {
        if (*str++ == *pattern) {
            start = str - 1;
            for (p = pattern + 1, i = len; *p && *str && i >= 0; i--, p++) {
                if (*p != *str++) {
                    break;
                }
            }
            if (*p == '\0') {
                return (char*) start;
            }
        }
    }
    return 0;
}


/*
 *  @copy   default
 *  
 *  Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.
 *  Copyright (c) Michael O'Brien, 1993-2011. All Rights Reserved.
 *  
 *  This software is distributed under commercial and open source licenses.
 *  You may use the GPL open source license described below or you may acquire 
 *  a commercial license from Embedthis Software. You agree to be fully bound 
 *  by the terms of either license. Consult the LICENSE.TXT distributed with 
 *  this software for full details.
 *  
 *  This software is open source; you can redistribute it and/or modify it 
 *  under the terms of the GNU General Public License as published by the 
 *  Free Software Foundation; either version 2 of the License, or (at your 
 *  option) any later version. See the GNU General Public License for more 
 *  details at: http://www.embedthis.com/downloads/gplLicense.html
 *  
 *  This program is distributed WITHOUT ANY WARRANTY; without even the 
 *  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
 *  
 *  This GPL license does NOT permit incorporating this software into 
 *  proprietary programs. If you are unable to comply with the GPL, you must
 *  acquire a commercial license to use this software. Commercial licenses 
 *  for this software and support services are available from Embedthis 
 *  Software at http://www.embedthis.com 
 *  
 *  Local variables:
    tab-width: 4
    c-basic-offset: 4
    End:
    vim: sw=4 ts=4 expandtab

    @end
 */
