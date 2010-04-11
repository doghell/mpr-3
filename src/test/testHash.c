/**
 *  testHash.c - Unit tests for the Hash class
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

/*********************************** Locals ***********************************/

#define HASH_COUNT  1000                /* Number of items to enter */

/************************************ Code ************************************/

static void testCreateTable(MprTestGroup *gp)
{
    MprHashTable    *table;

    table = mprCreateHash(gp, 211);
    assert(table != 0);
    mprFree(table);

    table = mprCreateHash(gp, 0);
    assert(table != 0);
    mprFree(table);

    table = mprCreateHash(gp, 1);
    assert(table != 0);
    mprFree(table);
}


static void testIsTableEmpty(MprTestGroup *gp)
{
    MprHashTable    *table;

    table = mprCreateHash(gp, 0);
    assert(table != 0);

    assert(mprGetHashCount(table) == 0);
    assert(mprGetFirstHash(table) == 0);
    assert(mprLookupHash(table, "") == 0);

    mprFree(table);
}


static void testInsertAndRemoveHash(MprTestGroup *gp)
{
    MprHashTable    *table;
    MprHash         *sp;
    cchar           *str;
    int             rc;

    table = mprCreateHash(gp, 0);
    assert(table != 0);

    /*
     *  Single insert
     */
    sp = mprAddHash(table, "Peter", "123 Madison Ave");
    assert(sp != 0);

    sp = mprGetFirstHash(table);
    assert(sp != 0);
    assert(sp->key != 0);
    assert(strcmp(sp->key, "Peter") == 0);
    assert(sp->data != 0);
    assert(strcmp(sp->data, "123 Madison Ave") == 0);

    /*
     *  Lookup
     */
    str = mprLookupHash(table, "Peter");
    assert(str != 0);
    assert(strcmp(str, "123 Madison Ave") == 0);

    rc = mprRemoveHash(table, "Peter");
    assert(rc == 0);

    assert(mprGetHashCount(table) == 0);
    sp = mprGetFirstHash(table);
    assert(sp == 0);

    str = mprLookupHash(table, "Peter");
    assert(str == 0);

    mprFree(table);
}


static void testHashScale(MprTestGroup *gp)
{
    MprHashTable    *table;
    MprHash         *sp;
    cchar           *str;
    char            address[80], name[80];
    int             i;

    table = mprCreateHash(gp, 0);
    assert(mprGetHashCount(table) == 0);

    /*
     *  All inserts below will insert allocated strings. We must free before
     *  deleting the table.
     */
    for (i = 0; i < HASH_COUNT; i++) {
        mprSprintf(name, sizeof(name), "name.%d", i);
        mprSprintf(address, sizeof(address), "%d Park Ave", i);
        sp = mprAddHash(table, name, address);
        assert(sp != 0);
    }
    assert(mprGetHashCount(table) == HASH_COUNT);

    /*
     *  Check data entered into the hash
     */
    for (i = 0; i < HASH_COUNT; i++) {
        mprSprintf(name, sizeof(name), "name.%d", i);
        str = mprLookupHash(table, name);
        assert(str != 0);
        mprSprintf(address, sizeof(address), "%d Park Ave", i);
        assert(strcmp(str, address) == 0);
    }

    mprFree(table);
}


static void testIterateHash(MprTestGroup *gp)
{
    MprHashTable    *table;
    MprHash         *sp;
    char            name[80], address[80];
    const char      *who, *where;
    int             count, i, check[HASH_COUNT];

    table = mprCreateHash(gp, 0);

    memset(check, 0, sizeof(check));

    /*
     *  Fill the table
     */
    for (i = 0; i < HASH_COUNT; i++) {
        mprSprintf(name, sizeof(name), "Bit longer name.%d", i);
        mprSprintf(address, sizeof(address), "%d Park Ave", i);
        sp = mprAddHash(table, name, mprStrdup(table, address));
        assert(sp != 0);
    }
    assert(mprGetHashCount(table) == HASH_COUNT);

    /*
     *  Check data entered into the table
     */
    sp = mprGetFirstHash(table);
    count = 0;
    while (sp) {
        assert(sp != 0);
        who = sp->key;
        where = sp->data;
        assert(isdigit((int) where[0]) != 0);
        i = atoi(where);
        check[i] = 1;
        sp = mprGetNextHash(table, sp);
        count++;
    }
    assert(count == HASH_COUNT);

    count = 0;
    for (i = 0; i < HASH_COUNT; i++) {
        if (check[i]) {
            count++;
        }
    }
    assert(count == 1000);
    
    mprFree(table);
}


MprTestDef testHash = {
    "symbol", 0, 0, 0,
    {
        MPR_TEST(0, testCreateTable),
        MPR_TEST(0, testIsTableEmpty),
        MPR_TEST(0, testInsertAndRemoveHash),
        MPR_TEST(0, testHashScale),
        MPR_TEST(0, testIterateHash),
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
