/**
 *  testMpr.c - program for the MPR unit tests
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

/****************************** Test Definitions ******************************/

extern MprTestDef testAlloc;
extern MprTestDef testBuf;
extern MprTestDef testEvent;
#if BLD_FEATURE_CMD
extern MprTestDef testCmd;
#endif
extern MprTestDef testFile;
extern MprTestDef testHash;
#if BLD_FEATURE_HTTP_CLIENT && DISABLED
extern MprTestDef testHttp;
#endif
extern MprTestDef testList;
extern MprTestDef testPath;
extern MprTestDef testSocket;
extern MprTestDef testSprintf;
extern MprTestDef testTime;
#if BLD_FEATURE_MULTITHREAD
extern MprTestDef testCond;
extern MprTestDef testLock;
extern MprTestDef testWorker;
#endif

static MprTestDef *testGroups[] = 
{
    &testAlloc,
    &testBuf,
#if BLD_FEATURE_MULTITHREAD
    &testCond,
#endif
#if BLD_FEATURE_CMD
    &testCmd,
#endif
    &testEvent,
    &testFile,
    &testPath,
    &testHash,
#if BLD_FEATURE_HTTP_CLIENT && DISABLED
    &testHttp,
#endif
    &testList,
#if BLD_FEATURE_MULTITHREAD
    &testLock,
    &testWorker,
#endif
    &testSocket,
    &testSprintf,
    &testTime,
    0
};
 
static MprTestDef master = {
    "api",
    testGroups,
    0, 0, 
    { { 0 } },
};


/************************************* Code ***********************************/

MAIN(testMain, int argc, char *argv[]) 
{
    Mpr             *mpr;
    MprTestService  *ts;
    MprTestGroup    *gp;
    int             rc;

    mpr = mprCreate(argc, argv, 0);

#if VXWORKS || WINCE
    /*
     *  These platforms pass an arg string in via the argc value. Assumes 32-bit.
     */
    mprMakeArgv(mpr, "testMpr", (char*) argc, &argc, &argv);
#endif

    ts = mprCreateTestService(mpr);
    if (ts == 0) {
        mprError(mpr, "Can't create test service");
        exit(2);
    }
    
    if (mprParseTestArgs(ts, argc, argv) < 0) {
        mprFree(mpr);
        exit(3);
    }
    
    gp = mprAddTestGroup(ts, &master);
    if (gp == 0) {
        exit(4);
    }

#if BLD_FEATURE_SSL && (BLD_FEATURE_MATRIXSSL || BLD_FEATURE_OPENSSL)
    if (!mprLoadSsl(mpr, 0)) {
        exit(5);
    }
#endif

    /*
     *  Need a background event thread as we use the main thread to run the tests.
     */
    if (mprStart(mpr, 0)) {
        mprError(mpr, "Can't start mpr services");
        exit(4);
    }

    /*
     *  Run the tests and return zero if 100% success
     */
    rc = mprRunTests(ts);
    mprReportTestResults(ts);

    return (rc == 0) ? 0 : 6;
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
