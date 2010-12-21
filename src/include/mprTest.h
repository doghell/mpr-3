/*
 *  mprTest.h - Header for the Embedthis Unit Test Framework
 *  
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/******************************************************************************/

#ifndef _h_MPR_TEST
#define _h_MPR_TEST 1

/********************************** Includes **********************************/

#include "mpr.h"

#if BLD_FEATURE_TEST
/********************************** Constants *********************************/
/*
 *  Tunable constants
 */
#define MPR_TEST_POLL_NAP   25
#define MPR_TEST_SLEEP      (60 * 1000)
#define MPR_TEST_MAX_STACK  (16)

/*
 *  Unit test definition structures
 */
struct MprTestGroup;
typedef void        (*MprTestProc)(struct MprTestGroup *tp);

typedef struct MprTestCase {
    char            *name;
    int             level;
    MprTestProc     proc;
    int             (*init)(struct MprTestGroup *gp);
    int             (*term)(struct MprTestGroup *gp);
} MprTestCase;

typedef struct MprTestDef {
    char                *name;
    struct MprTestDef   **groupDefs;
    int                 (*init)(struct MprTestGroup *gp);
    int                 (*term)(struct MprTestGroup *gp);
    MprTestCase         caseDefs[32];
} MprTestDef;


/*
 *  Assert macros for use by unit tests
 */
#undef  assert
#define assert(C)   assertTrue(gp, MPR_LOC, C, #C)

#define MPR_TEST(level, functionName) { #functionName, level, functionName, 0, 0 }

typedef struct MprTestService {
    int             argc;                   /* Count of arguments */
    char            **argv;                 /* Arguments for test */
    int             activeThreadCount;      /* Currently active test threads */
    char            *commandLine;
    bool            continueOnFailures;     /* Keep testing on failures */
    bool            debugOnFailures;        /* Break to the debugger */
    int             echoCmdLine;            /* Echo the command line */
    int             firstArg;               /* Count of arguments */
    MprList         *groups;                /* Master list of test groups */
    int             iterations;             /* Times to run the test */
    bool            singleStep;             /* Pause between tests */
    cchar           *name;                  /* Name for entire test */
    int             numThreads;             /* Number of test threads */
    int             workers;                /* Count of worker threads */
    MprTime         start;                  /* When testing began */
    int             testDepth;              /* Depth of entire test */
    MprList         *perThreadGroups;       /* Per thread copy of groups */
    int             totalFailedCount;       /* Total count of failing tests */
    int             totalTestCount;         /* Total count of all tests */
    MprList         *testFilter;            /* Test groups to run */
    int             verbose;                /* Output activity trace */

#if BLD_FEATURE_MULTITHREAD
    MprMutex        *mutex;                 /* Multi-thread sync */
#endif
} MprTestService;

extern MprTestService *mprCreateTestService(MprCtx ctx);
extern int          mprParseTestArgs(MprTestService *ts, int argc, char **argv);
extern int          mprRunTests(MprTestService *sp);
extern void         mprReportTestResults(MprTestService *sp);

/*
 *  A test group is a group of tests to cover a unit of functionality. A test group may contain other test groups.
 */
typedef struct MprTestGroup {
    char            *name;                  /* Name of test */
    char            *fullName;              /* Fully qualified name of test */
    int             testDepth;              /* Depth at which test should run */
    bool            skip;                   /* Skip this test */
    bool            skipWarned;             /* Warned that test will be skipped */

    bool            success;                /* Result of last run */
    int             failedCount;            /* Total failures of this test */
    int             testCount;              /* Count of tests */
    MprList         *failures;              /* List of all failures */

    MprTestService  *service;               /* Reference to the service */
    struct MprTestGroup *parent;            /* Parent test group */
    struct MprTestGroup *root;              /* Top level test group parent */

    MprList         *groups;                /* List of groups */
    MprList         *cases;                 /* List of tests in this group */
    MprTestDef      *def;                   /* Test definition ref */

    MprCond         *cond;                  /* Multi-thread sync */
    MprCond         *cond2;                 /* Second multi-thread sync */

#if BLD_FEATURE_HTTP_CLIENT
    MprHttp         *http;                  /* Http connection for this group */
    char            *content;               /* Cached response content */
#endif
    void            *data;                  /* Test specific data */
    MprCtx          ctx;                    /* Memory context for unit tests to use */
    
#if BLD_FEATURE_MULTITHREAD
    MprMutex        *mutex;                 /* Multi-thread sync */
#endif

} MprTestGroup;


extern MprTestGroup *mprAddTestGroup(MprTestService *ts, MprTestDef *def);
extern void         mprResetTestGroup(MprTestGroup *gp);
extern bool         assertTrue(MprTestGroup *gp, cchar *loc, bool success, cchar *msg);
extern void         mprSignalTestComplete(MprTestGroup *gp);
extern void         mprSignalTest2Complete(MprTestGroup *gp);
extern bool         mprWaitForTestToComplete(MprTestGroup *gp, int timeout);
extern bool         mprWaitForTest2ToComplete(MprTestGroup *gp, int timeout);


typedef struct MprTestFailure {
    char            *loc;
    char            *message;
} MprTestFailure;


#endif /* BLD_FEATURE_TEST */
#endif /* _h_MPR_TEST */


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
