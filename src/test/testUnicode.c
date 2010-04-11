/**
 *  testUnicode.c - Unit tests for Unicode
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

/************************************ Code ************************************/

static void testBasicUnicode(MprTestGroup *gp)
{
    char    buf[256];
    char    *str;
    int     count;

    mprSprintf(buf, sizeof(buf), "%d", 12345678);
    assert(strlen(buf) == 8);
    assert(strcmp(buf, "12345678") == 0);

    mprSprintf(buf, sizeof(buf), "%d", -12345678);
    assert(strlen(buf) == 9);
    assert(strcmp(buf, "-12345678") == 0);

    str = mprAsprintf(gp, 4, "%d", 12345678);
    count = strlen(str);
    assert(count == 3);
    assert(strcmp(str, "123") == 0);
    mprFree(str);
}


/*
 */
MprTestDef testUnicode = {
    "unicode", 0, 0, 0,
    {
        MPR_TEST(0, testBasicUnicode),
        MPR_TEST(0, 0),
    },
};
/*
 *  @copy   default
 *  
 *  Copyright (c) Embedthis Software LLC, 2003-2010. All Rights Reserved.
 *  Copyright (c) Michael O'Brien, 1993-2010. All Rights Reserved.
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
 *  @end
 */
