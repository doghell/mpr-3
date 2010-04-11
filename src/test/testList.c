/**
 *  testList.c - Unit tests for the List, Link and MprStringList classes
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

/*********************************** Locals ***********************************/

#define LIST_MAX_ITEMS      1000            /* Count of items to put in list */

/************************************ Code ************************************/

static void testCreateList(MprTestGroup *gp)
{
    MprList *lp;

    lp = mprCreateList(gp);
    assert(lp != 0);
    mprFree(lp);
}


static void testIsListEmpty(MprTestGroup *gp)
{
    MprList     *lp;

    lp = mprCreateList(gp);
    assert(lp != 0);

    assert(mprGetListCount(lp) == 0);
    assert(mprGetFirstItem(lp) == 0);

    mprFree(lp);
}


static void testInsertAndRemove(MprTestGroup *gp)
{
    MprList     *lp;
    int         index;

    lp = mprCreateList(gp);
    assert(lp != 0);

    /*
     *  Do one insert and remove 
     */
    index = mprAddItem(lp, (void*) 1);
    assert(index >= 0);
    assert(mprGetListCount(lp) == 1);

    mprRemoveItem(lp, (void*) 1);
    assert(mprGetListCount(lp) == 0);

    mprFree(lp);
}


static void testLotsOfInserts(MprTestGroup *gp)
{
    MprList     *lp;
    int         i;

    lp = mprCreateList(gp);
    assert(lp != 0);

    /*
     *  Do lots insertions 
     */
    for (i = 0; i < LIST_MAX_ITEMS; i++) {
        mprAddItem(lp, (void*) (long) i);
        assert(mprGetListCount(lp) == (i + 1));
    }

    /*
     *  Now remove
     */
    for (i = LIST_MAX_ITEMS - 1; i >= 0; i--) {
        mprRemoveItem(lp, (void*) (long) i);
        assert(mprGetListCount(lp) == i);
    }
    mprFree(lp);
}


static void testListIterate(MprTestGroup *gp)
{
    MprList     *lp;
    int         max, i, item, next;

    lp = mprCreateList(gp);
    assert(lp != 0);

    max = 50;
    for (i = 0; i < max; i++) {
        mprAddItem(lp, (void*) (long) (i + 1));
    }

    i = 0;
    next = 0;
    item = (int) (long) mprGetNextItem(lp, &next);
    while (item > 0) {
        i++;
        item = (int) (long) mprGetNextItem(lp, &next);
    }
    assert(i == max);


    /*
     *  Abbreviated form with no GetFirst
     */
    i = 0;
    next = 0;
    while ((item = (int) (long) mprGetNextItem(lp, &next)) != 0) {
        i++;
    }
    assert(i == max);

    mprFree(lp);
}


static void testOrderedInserts(MprTestGroup *gp)
{
    MprList     *lp;
    int         i, item, next;

    lp = mprCreateList(gp);
    assert(lp != 0);

    /*
     *  Add items such that the final list is ordered
     */
    mprAddItem(lp, (void*) (long) 4);
    mprAddItem(lp, (void*) (long) 5);
    mprInsertItemAtPos(lp, 0, (void*) 2);
    mprInsertItemAtPos(lp, 0, (void*) 1);
    mprInsertItemAtPos(lp, 2, (void*) 3);
    mprAddItem(lp, (void*) (long) 6);

    i = 1;
    next = 0;
    item = (int) (long) mprGetNextItem(lp, &next);
    while (item > 0) {
        assert(item == i);
        i++;
        item = (int) (long) mprGetNextItem(lp, &next);
    }
    mprFree(lp);
}


MprTestDef testList = {
    "list", 0, 0, 0,
    {
        MPR_TEST(0, testCreateList),
        MPR_TEST(0, testIsListEmpty),
        MPR_TEST(0, testInsertAndRemove),
        MPR_TEST(0, testLotsOfInserts),
        MPR_TEST(0, testListIterate),
        MPR_TEST(0, testOrderedInserts),
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
