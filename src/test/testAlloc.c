/**
 *  testAlloc.c - Unit tests for the mprAlloc module
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */ 

/********************************** Includes **********************************/

#include    "mprTest.h"

/************************************ Code ************************************/


static void testBasicAlloc(MprTestGroup *gp)
{
    char    *cp;
    int     size;

    size = 16;
    cp = (char*) mprAlloc(gp, size);
    assert(cp != 0);
    memset(cp, 0x77, size);
    mprFree(cp);

    cp = (char*) mprAlloc(gp, size);
    assert(cp != 0);
    memset(cp, 0x77, size);
    cp = (char*) mprRealloc(gp, cp, size * 2);
    assert(cp != 0);
    mprFree(cp);

    cp = mprStrdup(gp, "Hello World");
    assert(cp != 0);
    assert(strcmp(cp, "Hello World") == 0);
    mprFree(cp);

    /*
     *  Test special MR allowances
     */
    mprFree(0);
    cp = mprStrdup(gp, 0);
    assert(cp != 0);
    assert(cp[0] == '\0');
    mprFree(cp);
}



static void testLotsOfAlloc(MprTestGroup *gp)
{
    void    *mp;
    int     i;

    for (i = 0; i < 10000; i++) {
        mp = mprAlloc(gp, 64);
        assert(mp != 0);
        mprFree(mp);
    }
    for (i = 2; i < (2 * 1024 * 1024); i *= 2) {
        mp = mprAlloc(gp, i);
        assert(mp != 0);
        mprFree(mp);
    }
}


static void testBigAlloc(MprTestGroup *gp)
{
    void    *mp;

    mp = mprAlloc(gp, 8 * 1024 * 1024);
    assert(mp != 0);
    mprFree(mp);
}


static void testAllocIntegrityChecks(MprTestGroup *gp)
{
    void    *blocks[259];
    uchar   *cp;
    int     i, j, size, count;

    /*
     *  Basic integrity test. Allocate blocks of 64 bytes and fill and test
     *  each block
     */
    size = 64;
    count = sizeof(blocks) / sizeof(void*);
    for (i = 0; i < count; i++) {
        blocks[i] = mprAlloc(gp, size);
        assert(blocks[i] != 0);
        memset(blocks[i], i % 0xff, size);
    }
    for (i = 0; i < count; i++) {
        cp = (uchar*) blocks[i];
        for (j = 0; j < size; j++) {
            assert(cp[j] == (i % 0xff));
        }
        mprFree(blocks[i]);
    }

    /*
     *  Now do with bigger blocks and also free some before testing
     */
    count = sizeof(blocks) / sizeof(void*);
    for (i = 1; i < count; i++) {
        size = 1 << ((i + 6) / 100);
        blocks[i] = mprAlloc(gp, size);
        assert(blocks[i] != 0);
        memset(blocks[i], i % 0xff, size);
    }
    for (i = 1; i < count; i += 3) {
        mprFree(blocks[i]);
        blocks[i] = 0;
    }
    for (i = 1; i < count; i++) {
        if (blocks[i] == 0) {
            continue;
        }
        cp = (uchar*) blocks[i];
        size = 1 << ((i + 6) / 100);
        for (j = 0; j < size; j++) {
            assert(cp[j] == (i % 0xff));
        }
        mprFree(blocks[i]);
    }
}


MprTestDef testAlloc = {
    "alloc", 0, 0, 0,
    {
        MPR_TEST(0, testBasicAlloc),
        MPR_TEST(1, testLotsOfAlloc),
        MPR_TEST(2, testBigAlloc),
        MPR_TEST(0, testAllocIntegrityChecks),
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
