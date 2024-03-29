/**
 *  testThread.c - Threading Unit Tests 
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

/************************************ Code ************************************/

#if BLD_FEATURE_MULTITHREAD

static void workerProc(void *data, MprWorker *thread)
{
    mprSignalTestComplete((MprTestGroup*) data);
}


static void testStartWorker(MprTestGroup *gp)
{
    int     rc;

    /*
     *  Can only run this test if the worker is greater than the number of threads.
     */
    if (mprGetMaxWorkers(gp) > gp->service->numThreads) {
        rc = mprStartWorker(gp, workerProc, (void*) gp, MPR_NORMAL_PRIORITY);
        assert(rc == 0);
        assert(mprWaitForTestToComplete(gp, MPR_TEST_SLEEP));
    }
}


MprTestDef testWorker = {
    "worker", 0, 0, 0,
    {
        MPR_TEST(0, testStartWorker),
        MPR_TEST(0, 0),
    },
};

#else
void dummyTestWorker() {}
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
