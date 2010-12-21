/**
 *  testCond.c - Unit tests for mprCond
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

#if BLD_FEATURE_MULTITHREAD
/************************************ Code ************************************/

static int initCond(MprTestGroup *gp)
{
    return 0;
}


static int termCond(MprTestGroup *gp)
{
    return 0;
}


static void callback(void *data, MprEvent *event)
{
    mprSignalCond((MprCond*) data);
}


static void testCriticalSection(MprTestGroup *gp)
{
    MprCond     *cond;
    MprEvent    *event;
    int         rc, save;

    cond = mprCreateCond(gp);
    assert(cond != 0);
    mprAssert(cond->triggered == 0);

    /*
     *  Create an event to signal the condition var in 10 msec
     */
    event = mprCreateEvent(mprGetDispatcher(gp), callback, 0, 0, (void*) cond, 0);
    assert(event != 0);
    save = cond->triggered;
    rc = mprWaitForCondWithService(cond, MPR_TEST_TIMEOUT);
    assert(rc == 0);
    mprFree(cond);
    mprFree(event);
}


MprTestDef testCond = {
    "cond", 0, initCond, termCond,
    {
        MPR_TEST(0, testCriticalSection),
        MPR_TEST(0, 0),
    },
};


#endif /* BLD_FEATURE_MULTITHREAD */
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
