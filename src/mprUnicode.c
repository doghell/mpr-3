/**
 *  mprUnicode.c - Unicode 
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/
#if KEEP

#include    "mpr.h"

/************************************ Code ************************************/
/*
 *  Allocate a new (empty) unicode string
 */
uni *mprAllocUs(MprCtx ctx)
{
    mprAssert(ctx);

    return mprAllocObjZeroed(ctx, uni);
}



/*
 *  Grow the string buffer for a unicode string
 */
static int growUs(uni *us, int len)
{
    mprAssert(us);
    mprAssert(len >= 0);

    if (len < us->length) {
        return 0;
    }

    us->str = mprRealloc(us, us->str, len);
    if (us->str == 0) {
        return MPR_ERR_NO_MEMORY;
    }
    us->length = len;
    return 0;
}



/*
 *  Convert a ASCII string to UTF-8/16
 */
static int memToUs(uni *us, const uchar *str, int len)
{
    uniData   *up;
    cchar       *cp;

    mprAssert(us);
    mprAssert(str);

    if (len > us->length) {
        if (growUs(us, len) < 0) {
            return MPR_ERR_NO_MEMORY;
        }
    }
    us->length = len;

#if BLD_FEATURE_UTF16
    cp = (cchar*) str;
    up = us->str;
    while (*cp) {
        *up++ = *cp++;
    }
#else
    memcpy((void*) us->str, str, len);
#endif

    return 0;
}



/*
 *  Convert a C string to a newly allocated unicode string
 */
uni *mprStrToUs(MprCtx ctx, cchar *str)
{
    uni   *us;
    int     len;

    mprAssert(ctx);
    mprAssert(str);

    us = mprAllocUs(ctx);
    if (us == 0) {
        return 0;
    }

    if (str == 0) {
        str = "";
    }

    len = strlen(str);

    if (memToUs(us, (const uchar*) str, len) < 0) {
        return 0;
    }
    
    return us;
}



/*
 *  Convert a memory buffer to a newly allocated unicode string
 */
uni *mprMemToUs(MprCtx ctx, const uchar *buf, int len)
{
    uni   *us;

    mprAssert(ctx);
    mprAssert(buf);

    us = mprAllocUs(ctx);
    if (us == 0) {
        return 0;
    }

    if (memToUs(us, buf, len) < 0) {
        return 0;
    }
    
    return us;
}



/*
 *  Convert a unicode string newly allocated C string
 */
char *mprUsToStr(uni *us)
{
    char    *str, *cp;

    mprAssert(us);

    str = cp = mprAlloc(us, us->length + 1);
    if (cp == 0) {
        return 0;
    }

#if BLD_FEATURE_UTF16
{
    uniData   *up;
    int         i;

    up = us->str;
    for (i = 0; i < us->length; i++) {
        cp[i] = up[i];
    }
}
#else
    mprStrcpy(cp, us->length, us->str);
#endif
    return str;
}



/*
 *  Copy one unicode string to another. No allocation
 */
static void copyUs(uni *dest, uni *src)
{
    mprAssert(dest);
    mprAssert(src);
    mprAssert(dest->length <= src->length);
    mprAssert(dest->str);
    mprAssert(src->str);

    memcpy(dest->str, src->str, src->length * sizeof(uniData));
    dest->length = src->length;
}



/*
 *  Copy one unicode string to another. Grow the destination unicode string buffer as required.
 */
int mprCopyUs(uni *dest, uni *src)
{
    mprAssert(dest);
    mprAssert(src);

    dest->length = src->length;

    if (src->length > dest->length) {
        if (growUs(dest, src->length) < 0) {
            return MPR_ERR_NO_MEMORY;
        }
    }

    copyUs(dest, src);

    return 0;
}



/*
 *  Catenate a unicode string onto another.
 */
int mprCatUs(uni *dest, uni *src)
{
    int     len;

    len = dest->length + src->length;
    if (growUs(dest, len) < 0) {
        return MPR_ERR_NO_MEMORY;
    }

    memcpy(&dest->str[dest->length], src->str, src->length * sizeof(uniData));
    dest->length += src->length;

    return 0;
}



/*
 *  Catenate a set of unicode string arguments onto another.
 */
int mprCatUsArgs(uni *dest, uni *src, ...)
{
    va_list     args;
    uni       *us;
    int         len;

    va_start(args, src);

    len = 0;
    us = src;
    for (us = src; us; ) {
        us = va_arg(args, uni*);
        len += us->length;
    }

    if (growUs(dest, len) < 0) {
        return MPR_ERR_NO_MEMORY;
    }

    va_start(args, src);
    for (us = src; us; ) {
        us = va_arg(args, uni*);
        
        memcpy(&dest->str[dest->length], src->str, src->length * sizeof(uniData));
        dest->length += src->length;
    }

    va_end(args);
    
    return 0;
}



/*
 *  Duplicate a unicode string by allocating a new unicode string and copying the source data.
 */
uni *mprDupUs(uni *src)
{
    uni   *dest;

    dest = mprAllocUs(src);
    if (dest == 0) {
        return 0;
    }

    copyUs(dest, src);

    return dest;
}




/*
 *  Copy a C string into an existing unicode string.
 */
int mprCopyStrToUs(uni *dest, cchar *str)
{
    mprAssert(dest);
    mprAssert(str);

    return memToUs(dest, (const uchar*) str, strlen(str));
}



/*
 *  Return the lenght of a unicoded string.
 */
int mprGetUsLength(uni *us)
{
    mprAssert(us);

    return us->length;
}




/*
 *  Return the index in a unicode string of a given unicode character code. Return -1 if not found.
 */
int mprContainsChar(uni *us, int charPat)
{
    int     i;

    mprAssert(us);

    for (i = 0; i < us->length; i++) {
        if (us->str[i] == charPat) {
            return i;
        }
    }
    return -1;
}



/*
 *  Return TRUE if a unicode string contains a given unicode string.
 */
int mprContainsUs(uni *us, uni *pat)
{
    int     i, j;

    mprAssert(us);
    mprAssert(pat);
    mprAssert(pat->str);

    if (pat == 0 || pat->str == 0) {
        return 0;
    }
    
    for (i = 0; i < us->length; i++) {
        for (j = 0; j < pat->length; j++) {
            if (us->str[i] != pat->str[j]) {
                break;
            }
        }
        if (j == pat->length) {
            return i;
        }
    }
    return -1;
}



/*
 *  Return TRUE if a unicode string contains a given unicode string after doing a case insensitive comparison.
 */
int mprContainsCaselessUs(uni *us, uni *pat)
{
    int     i, j;

    mprAssert(us);
    mprAssert(pat);
    mprAssert(pat->str);

    for (i = 0; i < us->length; i++) {
        for (j = 0; j < pat->length; j++) {
            if (tolower(us->str[i]) != tolower(pat->str[j])) {
                break;
            }
        }
        if (j == pat->length) {
            return i;
        }
    }
    return -1;
}



/*
 *  Return TRUE if a unicode string contains a given C string.
 */
int mprContainsStr(uni *us, cchar *pat)
{
    int     i, j, len;

    mprAssert(us);
    mprAssert(pat);

    if (pat == 0 || *pat == '\0') {
        return 0;
    }
    
    len = strlen(pat);
    
    for (i = 0; i < us->length; i++) {
        for (j = 0; j < len; j++) {
            if (us->str[i] != pat[j]) {
                break;
            }
        }
        if (j == len) {
            return i;
        }
    }
    return -1;
}



#if FUTURE
int mprContainsPattern(uni *us, MprRegex *pat)
{
    return 0;
}
#endif




uni *mprTrimUs(uni *us, uni *pat)
{
    return 0;
}



int mprTruncateUs(uni *us, int len)
{
    mprAssert(us);

    mprAssert(us->length >= len);

    if (us->length < len) {
        return MPR_ERR_WONT_FIT;
    }

    us->length = len;
    return 0;
}



uni *mprSubUs(uni *src, int start, int len)
{
    uni   *dest;

    mprAssert(src);
    mprAssert(start >= 0);
    mprAssert(len > 0);
    mprAssert((start + len) <= src->length);

    if ((start + len) > src->length) {
        return 0;
    }

    dest = mprAllocUs(src);
    if (dest == 0) {
        return 0;
    }

    if (growUs(dest, len) < 0) {
        mprFree(dest);
        return 0;
    }
    memcpy(dest->str, &src->str[start], len * sizeof(uniData));
    dest->length = len;

    return dest;
}







void mprUsToLower(uni *us)
{
    int     i;

    mprAssert(us);
    mprAssert(us->str);

    for (i = 0; i < us->length; i++) {
        us->str[i] = tolower(us->str[i]);
    }
}



void mprUsToUpper(uni *us)
{
    int     i;

    mprAssert(us);
    mprAssert(us->str);

    for (i = 0; i < us->length; i++) {
        us->str[i] = toupper(us->str[i]);
    }
}




uni *mprTokenizeUs(uni *us, uni *delim, int *last)
{
    return 0;
}




int mprFormatUs(uni *us, int maxSize, cchar *fmt, ...)
{
    return 0;
}



int mprScanUs(uni *us, cchar *fmt, ...)
{
    return 0;
}



/******************************************************************************/
#else
void __mprDummyUnicode() {}
#endif

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
