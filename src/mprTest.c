/*
 *  mprTestLib.c - Embedthis Unit Test Framework Library
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

#if BLD_FEATURE_TEST
/***************************** Forward Declarations ***************************/

static void     adjustFailedCount(MprTestService *sp, int adj);
static void     adjustThreadCount(MprTestService *sp, int adj);
static void     buildFullNames(MprTestGroup *gp, cchar *runName);
static MprTestFailure *createFailure(MprTestGroup *gp, cchar *loc, cchar *message);
static MprTestGroup *createTestGroup(MprTestService *sp, MprTestDef *def, MprTestGroup *parent);
static bool     filterTestGroup(MprTestGroup *gp);
static bool     filterTestCast(MprTestGroup *gp, MprTestCase *tc);
static char     *getErrorMessage(MprTestGroup *gp);
static int      parseFilter(MprTestService *sp, cchar *str);
static void     runTestGroup(MprTestGroup *gp);
static void     runTestProc(MprTestGroup *gp, MprTestCase *test);
static void     runTestThread(MprList *groups, void *threadp);
static int      setLogging(Mpr *mpr, char *logSpec);

#if BLD_FEATURE_MULTITHREAD
static MprList  *copyGroups(MprTestService *sp, MprList *groups);
#endif

/******************************************************************************/

MprTestService *mprCreateTestService(MprCtx ctx)
{
    MprTestService      *sp;

    sp = mprAllocObjZeroed(ctx, MprTestService);
    if (sp == 0) {
        return 0;
    }

    sp->iterations = 1;
    sp->numThreads = 1;
    sp->workers = 0;
    sp->testFilter = mprCreateList(sp);
    sp->groups = mprCreateList(sp);
    sp->start = mprGetTime(sp);

#if BLD_FEATURE_MULTITHREAD
    sp->mutex = mprCreateLock(sp);
#endif
    return sp;
}


int mprParseTestArgs(MprTestService *sp, int argc, char *argv[])
{
    Mpr         *mpr;
    cchar       *programName;
    char        *argp, *logSpec;
    int         err, i, depth, nextArg, outputVersion;

    i = 0;
    err = 0;
    outputVersion = 0;
    logSpec = "stderr:1";

    mpr = mprGetMpr(sp);
    programName = mprGetPathBase(mpr, argv[0]);

    sp->name = BLD_PRODUCT;

    /*
     *  Save the command line
     */
    sp->commandLine = mprStrcat(sp, -1, mprGetPathBase(mpr, argv[i++]), NULL);
    for (; i < argc; i++) {
        sp->commandLine = mprReallocStrcat(sp, -1, sp->commandLine, " ", argv[i], NULL);
    }

    for (nextArg = 1; nextArg < argc; nextArg++) {
        argp = argv[nextArg];

        if (strcmp(argp, "--continue") == 0) {
            sp->continueOnFailures = 1; 

        } else if (strcmp(argp, "--depth") == 0) {
            if (nextArg >= argc) {
                err++;
            } else {
                depth = atoi(argv[++nextArg]);
                if (depth < 0 || depth > 10) {
                    mprError(sp, "Bad test depth %d, (range 0-9)", depth);
                    err++;
                } else {
                    sp->testDepth = depth;
                }
            }

        } else if (strcmp(argp, "--debug") == 0 || strcmp(argp, "-d") == 0) {
            mprSetDebugMode(mpr, 1);
            sp->debugOnFailures = 1;

        } else if (strcmp(argp, "--echo") == 0) {
            sp->echoCmdLine = 1;

        } else if (strcmp(argp, "--filter") == 0 || strcmp(argp, "-f") == 0) {
            if (nextArg >= argc) {
                err++;
            } else {
                if (parseFilter(sp, argv[++nextArg]) < 0) {
                    err++;
                }
            }

        } else if (strcmp(argp, "--iterations") == 0 || (strcmp(argp, "-i") == 0)) {
            if (nextArg >= argc) {
                err++;
            } else {
                sp->iterations = atoi(argv[++nextArg]);
            }

        } else if (strcmp(argp, "--log") == 0 || strcmp(argp, "-l") == 0) {
            if (nextArg >= argc) {
                err++;
            } else {
                setLogging(mpr, argv[++nextArg]);
            }

        } else if (strcmp(argp, "--name") == 0) {
            if (nextArg >= argc) {
                err++;
            } else {
                sp->name = argv[++nextArg];
            }

        } else if (strcmp(argp, "--step") == 0 || strcmp(argp, "-s") == 0) {
            sp->singleStep = 1; 

        } else if (strcmp(argp, "--threads") == 0 || strcmp(argp, "-t") == 0) {
            if (nextArg >= argc) {
                err++;
            } else {
                i = atoi(argv[++nextArg]);
                if (i <= 0 || i > 100) {
                    mprError(sp, "%s: Bad number of threads (1-100)", programName);
                    return MPR_ERR_BAD_ARGS;
                }
#if BLD_FEATURE_MULTITHREAD
                sp->numThreads = i;
#else
                if (i > 1) {
                    mprLog(sp, 0, "%s: Program built single-threaded. Ignoring threads directive", programName);
                }
#endif
            }

        } else if (strcmp(argp, "--verbose") == 0 || strcmp(argp, "-v") == 0) {
            sp->verbose++;

        } else if (strcmp(argp, "--version") == 0 || strcmp(argp, "-V") == 0) {
            outputVersion++;

        } else if (strcmp(argp, "--workers") == 0 || strcmp(argp, "-w") == 0) {
            if (nextArg >= argc) {
                err++;
            } else {
                i = atoi(argv[++nextArg]);
                if (i < 0 || i > 100) {
                    mprError(sp, "%s: Bad number of worker threads (0-100)", programName);
                    return MPR_ERR_BAD_ARGS;
                }
#if BLD_FEATURE_MULTITHREAD
                sp->workers = i;
#else
                if (i > 1) {
                    mprLog(sp, 0, "%s: Program built single-threaded. Ignoring threads directive", programName);
                }
#endif
            }

        } else if (strcmp(argp, "-?") == 0 || (strcmp(argp, "--help") == 0 || strcmp(argp, "--?") == 0)) {
            err++;

        } else {
            /* Ignore unknown args */
        }
    }

    if (sp->workers == 0) {
        sp->workers = 2 + sp->numThreads * 2;
    }
#if LOAD_TEST_PACKAGES
    /* Must be at least one test module to load */
    if (nextArg >= argc) {
        err++;
    }
#endif

    if (err) {
        mprPrintfError(mpr, 
        "usage: %s [options]\n"
        "    --continue            # Continue on errors\n"
        "    --depth number        # Zero == basic, 1 == throrough, 2 extensive\n"
        "    --debug               # Run in debug mode\n"
        "    --echo                # Echo the command line\n"
        "    --filter pattern      # Filter tests by pattern x.y.z...\n"
        "    --iterations count    # Number of iterations to run the test\n"
        "    --log logFile:level   # Log to file file at verbosity level\n"
        "    --name testName       # Set test name\n"
        "    --step                # Single step tests\n"
        "    --threads count       # Number of test threads\n"
        "    --verbose             # Verbose mode\n"
        "    --version             # Output version information\n"
        "    --workers count       # Set maximum worker threads\n\n",
        programName);
        return MPR_ERR_BAD_ARGS;
    }

    if (outputVersion) {
        mprPrintfError(mpr, "%s: Version: %s\n", BLD_NAME, BLD_VERSION);
        mprFree(mpr);
        return MPR_ERR_BAD_ARGS;
    }

    sp->argc = argc;
    sp->argv = argv;
    sp->firstArg = nextArg;

#if LOAD_TEST_PACKAGES
    for (i = nextArg; i < argc; i++) {
        if (loadModule(sp, argv[i]) < 0) {
            return MPR_ERR_CANT_OPEN;
        }
    }
#endif
    
#if BLD_FEATURE_MULTITHREAD
    mprSetMaxWorkers(sp, sp->workers);
#endif

    return 0;
}


static int parseFilter(MprTestService *sp, cchar *filter)
{
    char    *str, *word, *tok;

    mprAssert(filter);
    if (filter == 0 || *filter == '\0') {
        return 0;
    }

    tok = 0;
    str = mprStrdup(sp, filter);
    word = mprStrTok(str, " \t\r\n", &tok);
    while (word) {
        if (mprAddItem(sp->testFilter, mprStrdup(sp, word)) < 0) {
            return MPR_ERR_NO_MEMORY;
        }
        word = mprStrTok(0, " \t\r\n", &tok);
    }
    mprFree(str);
    return 0;
}


#if LOAD_TEST_PACKAGES
static int loadModule(MprTestService *sp, cchar *fileName)
{
    char    *cp, *base, entry[MPR_MAX_FNAME], path[MPR_MAX_FNAME];

    mprAssert(fileName && *fileName);

    base = mprGetPathBase(sp, fileName);
    mprAssert(base);
    if ((cp = strrchr(base, '.')) != 0) {
        *cp = '\0';
    }
    if (mprLookupModule(sp, base)) {
        return 0;
    }
                
    mprSprintf(entry, sizeof(entry), "%sInit", base);

    if (fileName[0] == '/' || (*fileName && fileName[1] == ':')) {
        mprSprintf(path, sizeof(path), "%s%s", fileName, BLD_BUILD_SHOBJ);
    } else {
        mprSprintf(path, sizeof(path), "./%s%s", fileName, BLD_BUILD_SHOBJ);
    }
    if (mprLoadModule(sp, path, entry, (void*) sp) == 0) {
        mprError(sp, "Can't load module %s", path);
        return -1;
    }
    return 0;
}
#endif


int mprRunTests(MprTestService *sp)
{
    MprTestGroup    *gp;
    int             next;
#if BLD_FEATURE_MULTITHREAD
    MprThread       *tp;
#endif

    /*
     *  Build the full names for all groups
     */
    next = 0; 
    while ((gp = mprGetNextItem(sp->groups, &next)) != 0) {
        buildFullNames(gp, gp->name);
    }

    sp->activeThreadCount = sp->numThreads;

    if (sp->echoCmdLine) {
        mprPrintf(sp, "%12s %s ... ", "[Test]", sp->commandLine);
        if (sp->verbose) {
            mprPrintf(sp, "\n");
        }
    }
    sp->start = mprGetTime(sp);

#if BLD_FEATURE_MULTITHREAD
{
    int i;
    /*
     *  Create worker threads for each test thread. 
     */
    for (i = 0; i < sp->numThreads; i++) {
        MprList     *lp;
        char        tName[64];

        mprSprintf(tName, sizeof(tName), "test.%d", i);

        lp = copyGroups(sp, sp->groups);
        if (lp == 0) {
            return MPR_ERR_NO_MEMORY;
        }
        
        /*
         *  Build the full names for all groups
         */
        next = 0; 
        while ((gp = mprGetNextItem(lp, &next)) != 0) {
            buildFullNames(gp, gp->name);
        }
        tp = mprCreateThread(sp, tName, (MprThreadProc) runTestThread, (void*) lp, MPR_NORMAL_PRIORITY, 0);
        if (tp == 0) {
            return MPR_ERR_NO_MEMORY;
        }
        if (mprStartThread(tp) < 0) {
            mprError(sp, "Can't start thread %d", i);
            return MPR_ERR_CANT_INITIALIZE;
        }
    }

    /*
     *  Wait for all the threads to complete (simple but effective)
     */
    while (sp->activeThreadCount > 0) {
        mprServiceEvents(mprGetDispatcher(sp), 250, MPR_SERVICE_EVENTS | MPR_SERVICE_IO);
    }
}
#else
    runTestThread(sp->groups, 0);
#endif

    return (sp->totalFailedCount == 0) ? 0 : 1;
}


#if BLD_FEATURE_MULTITHREAD
static MprList *copyGroups(MprTestService *sp, MprList *groups)
{
    MprTestGroup    *gp, *newGp;
    MprList         *lp;
    int             next;

    lp = mprCreateList(sp);
    if (lp == 0) {
        return 0;
    }

    next = 0; 
    while ((gp = mprGetNextItem(groups, &next)) != 0) {
        newGp = createTestGroup(sp, gp->def, NULL);
        if (newGp == 0) {
            mprFree(lp);
            return 0;
        }
        if (mprAddItem(lp, newGp) < 0) {
            mprFree(lp);
            return 0;
        }
    }
    return lp;
}
#endif


/*
 *  Run the test groups. One invocation per thread. Used even if not multithreaded.
 */
void runTestThread(MprList *groups, void *threadp)
{
    MprTestService  *sp;
    MprTestGroup    *gp;
    int             next, i;

    /*
     *  Get the service pointer
     */
    gp = mprGetFirstItem(groups);
    if (gp == 0) {
        return;
    }
    sp = gp->service;
    mprAssert(sp);

    for (i = (sp->iterations + sp->numThreads - 1) / sp->numThreads; i > 0; i--) {
        if (sp->totalFailedCount > 0 && !sp->continueOnFailures) {
            break;
        }
        next = 0; 
        while ((gp = mprGetNextItem(groups, &next)) != 0) {
            runTestGroup(gp);
        }
    }

    if (threadp) {
        adjustThreadCount(sp, -1);
    }
}


void mprReportTestResults(MprTestService *sp)
{
    if (sp->verbose <= 1 && sp->totalFailedCount == 0) {
        if (sp->verbose > 1) {
            mprPrintf(sp, "\n");
            mprPrintf(sp, "PASSED all tests for \"%s\"\n", sp->name);
        }
    }
    if (sp->verbose >= 2) {
        double  elapsed;
        elapsed = ((mprGetTime(sp) - sp->start) * 1.0 / 1000.0);
        mprPrintf(sp, "%s: %d tests completed, %d test(s) failed. ", 
            mprGetAppName(sp), sp->totalTestCount, sp->totalFailedCount);
        mprPrintf(sp, "Elapsed time: %5.2f seconds.\n", elapsed);
    }
}


static void buildFullNames(MprTestGroup *gp, cchar *name)
{
    MprTestGroup    *np;
    char            *nameBuf;
    cchar           *nameStack[MPR_TEST_MAX_STACK];
    int             tos, nextItem;

    tos = 0;

    /*
     *  Build the full name for this case
     */
    nameStack[tos++] = name;
    for (np = gp->parent; np && np != np->parent && tos < MPR_TEST_MAX_STACK;  np = np->parent) {
        nameStack[tos++] = np->name;
    }
    nameBuf = mprStrdup(gp, gp->service->name);
    while (--tos >= 0) {
        nameBuf = mprReallocStrcat(gp, -1, nameBuf, ".", nameStack[tos], NULL);
    }
    mprAssert(gp->fullName == 0);
    gp->fullName = mprStrdup(gp, nameBuf);

    /*
     *  Recurse for all test case groups
     */
    nextItem = 0;
    np = mprGetNextItem(gp->groups, &nextItem);
    while (np) {
        buildFullNames(np, np->name);
        np = mprGetNextItem(gp->groups, &nextItem);
    }
}


MprTestGroup *mprAddTestGroup(MprTestService *sp, MprTestDef *def)
{
    MprTestGroup    *gp;

    gp = createTestGroup(sp, def, NULL);
    if (gp == 0) {
        return 0;
    }

    if (mprAddItem(sp->groups, gp) < 0) {
        mprFree(gp);
        return 0;
    }
    return gp;
}


static MprTestGroup *createTestGroup(MprTestService *sp, MprTestDef *def, MprTestGroup *parent)
{
    MprTestGroup    *gp, *child;
    MprTestDef      **dp;
    MprTestCase     *tc;

    gp = mprAllocObjZeroed(sp, MprTestGroup);
    if (gp == 0) {
        return 0;
    }
    gp->service = sp;
    gp->cond = mprCreateCond(gp);

    gp->failures = mprCreateList(sp);
    if (gp->failures == 0) {
        mprFree(gp);
        return 0;
    }

    gp->cases = mprCreateList(sp);
    if (gp->cases == 0) {
        mprFree(gp);
        return 0;
    }

    gp->groups = mprCreateList(sp);
    if (gp->groups == 0) {
        mprFree(gp);
        return 0;
    }

    gp->def = def;
    gp->name = mprStrdup(sp, def->name);
    gp->success = 1;

#if BLD_FEATURE_HTTP_CLIENT
    if (parent == 0) {
        gp->http = mprCreateHttp(gp);
        mprSetHttpFollowRedirects(gp->http, 0);
    } else {
        gp->http = parent->http;
    }
#endif

    for (tc = def->caseDefs; tc->proc; tc++) {
        if (mprAddItem(gp->cases, tc) < 0) {
            mprFree(gp);
            return 0;
        }
    }

    if (def->groupDefs) {
        for (dp = &def->groupDefs[0]; *dp && (*dp)->name; dp++) {
            child = createTestGroup(sp, *dp, gp);
            if (child == 0) {
                mprFree(gp);
                return 0;
            }
            if (mprAddItem(gp->groups, child) < 0) {
                mprFree(gp);
                return 0;
            }
            child->parent = gp;
            child->root = gp->parent;
        }
    }
    return gp;
}


void mprResetTestGroup(MprTestGroup *gp)
{
    gp->success = 1;

#if BLD_FEATURE_MULTITHREAD
    if (gp->mutex) {
        mprFree(gp->mutex);
    }
    gp->mutex = mprCreateLock(gp);
#endif
}


static void runTestGroup(MprTestGroup *parent)
{
    MprTestService  *sp;
    MprTestGroup    *gp, *nextGroup;
    MprTestCase     *tc;
    int             count, nextItem;

    sp = parent->service;

    if (parent->def->init && (*parent->def->init)(parent) < 0) {
        parent->failedCount++;
        return;
    }

    /*
     *  Recurse over sub groups
     */
    nextItem = 0;
    gp = mprGetNextItem(parent->groups, &nextItem);
    while (gp && (parent->success || sp->continueOnFailures)) {
        nextGroup = mprGetNextItem(parent->groups, &nextItem);
        if (gp->testDepth > sp->testDepth) {
            gp = nextGroup;
            continue;
        }

        /*
         *  See if this group has been filtered for execution
         */
        if (! filterTestGroup(gp)) {
            gp = nextGroup;
            continue;
        }
        count = sp->totalFailedCount;
        if (count > 0 && !sp->continueOnFailures) {
            if (parent->def->term) {
                (*parent->def->term)(parent);
            }
            return;
        }

        /*
         *  Recurse over all tests in this group
         */
        runTestGroup(gp);

        gp->testCount++;

        if (! gp->success) {
            /*  Propagate the failures up the parent chain */
            parent->failedCount++;
            parent->success = 0;
        }
        gp = nextGroup;
    }

    /*
     *  Run test cases for this group
     */
    nextItem = 0;
    tc = mprGetNextItem(parent->cases, &nextItem);
    while (tc && (parent->success || sp->continueOnFailures)) {
        if (parent->testDepth <= sp->testDepth) {
            if (filterTestCast(parent, tc)) {
                runTestProc(parent, tc);
            }
        }
        tc = mprGetNextItem(parent->cases, &nextItem);
    }

    if (parent->def->term && (*parent->def->term)(parent) < 0) {
        parent->failedCount++;
    }
}


/*
 *  Return true if we are to run the test group
 */
static bool filterTestGroup(MprTestGroup *gp)
{
    MprTestService  *sp;
    MprList         *testFilter;
    char            *pattern;
    int             len, next;

    sp = gp->service;
    testFilter = sp->testFilter;

    if (testFilter == 0) {
        return 1;
    }

    /*
     *  See if this test has been filtered
     */
    if (mprGetListCount(testFilter) > 0) {
        next = 0;
        pattern = mprGetNextItem(testFilter, &next);
        while (pattern) {
            len = min((int) strlen(pattern), (int) strlen(gp->fullName));
            if (mprStrcmpAnyCaseCount(gp->fullName, pattern, len) == 0) {
                break;
            }
            pattern = mprGetNextItem(testFilter, &next);
        }
        if (pattern == 0) {
            return 0;
        }
    }
    return 1;
}


/*
 *  Return true if we are to run the test case
 */
static bool filterTestCast(MprTestGroup *gp, MprTestCase *tc)
{
    MprTestService  *sp;
    MprList         *testFilter;
    char            *pattern, *fullName;
    int             len, next;

    sp = gp->service;
    testFilter = sp->testFilter;

    if (testFilter == 0) {
        return 1;
    }

    /*
     *  See if this test has been filtered
     */
    if (mprGetListCount(testFilter) > 0) {
        fullName = mprAsprintf(gp, -1, "%s.%s", gp->fullName, tc->name);
        next = 0;
        pattern = mprGetNextItem(testFilter, &next);
        while (pattern) {
            len = min((int) strlen(pattern), (int) strlen(fullName));
            if (mprStrcmpAnyCaseCount(fullName, pattern, len) == 0) {
                break;
            }
            pattern = mprGetNextItem(testFilter, &next);
        }
        mprFree(fullName);
        if (pattern == 0) {
            return 0;
        }
    }
    return 1;
}


static void runTestProc(MprTestGroup *gp, MprTestCase *test)
{
    MprTestService      *sp;

    if (test->proc == 0) {
        return;
    }
    sp = gp->service;

    mprResetTestGroup(gp);

    if (sp->singleStep) {
        mprPrintf(gp, "%12s Run test \"%s.%s\", press <ENTER>: ", "[Test]", gp->fullName, test->name);
        getchar();

    } else if (sp->verbose) {
        mprPrintf(gp, "%12s Run test \"%s.%s\": ", "[Test]", gp->fullName, test->name);
    }

    if (gp->skip) {
        if (sp->verbose) {
            if (gp->skipWarned++ == 0) {
                mprPrintf(gp, "%12s Skipping test: \"%s.%s\": \n", "[Skip]", gp->fullName, test->name);
            }
        }
        
    } else {
        /*
         *  The function is part of the enclosing MprTest group
         */
        mprResetCond(gp->cond);
        (test->proc)(gp);
    
        mprLock(sp->mutex);
        if (gp->success) {
            ++sp->totalTestCount;
            if (sp->verbose) {
                mprPrintf(sp, "PASSED\n");
            }
        } else {
            mprPrintfError(gp, "FAILED test \"%s.%s\"\nDetails: %s\n", gp->fullName, test->name, getErrorMessage(gp));
        }
    }
    mprUnlock(sp->mutex);
}


static char *getErrorMessage(MprTestGroup *gp)
{
    MprTestFailure  *fp;
    char            msg[MPR_MAX_STRING], *errorMsg;
    int             nextItem;

    nextItem = 0;
    errorMsg = mprStrdup(gp, "");
    fp = mprGetNextItem(gp->failures, &nextItem);
    while (fp) {
        mprSprintf(msg, sizeof(msg), "Failure in %s\nAssertion: \"%s\"\n", fp->loc, fp->message);
        if ((errorMsg = mprStrcat(gp, -1, msg, NULL)) == NULL) {
            break;
        }
        fp = mprGetNextItem(gp->failures, &nextItem);
    }
    return errorMsg;
}


static int addFailure(MprTestGroup *gp, cchar *loc, cchar *message)
{
    MprTestFailure  *fp;

    fp = createFailure(gp, loc, message);
    if (fp == 0) {
        mprAssert(fp);
        return MPR_ERR_NO_MEMORY;
    }
    mprAddItem(gp->failures, fp);
    return 0;
}


static MprTestFailure *createFailure(MprTestGroup *gp, cchar *loc, cchar *message)
{
    MprTestFailure  *fp;

    fp = mprAllocObj(gp, MprTestFailure);
    if (fp == 0) {
        return 0;
    }
    fp->loc = mprStrdup(fp, loc);
    fp->message = mprStrdup(fp, message);
    return fp;
}


bool assertTrue(MprTestGroup *gp, cchar *loc, bool isTrue, cchar *msg)
{
    if (! isTrue) {
        gp->success = isTrue;
    }
    if (! isTrue) {
        if (gp->service->debugOnFailures) {
            mprBreakpoint();
        }
        addFailure(gp, loc, msg);
        gp->failedCount++;
        adjustFailedCount(gp->service, 1);
    }
    return isTrue;
}


bool mprWaitForTestToComplete(MprTestGroup *gp, int timeout)
{
    int     rc;
    
    mprAssert(gp->cond);

    rc = (mprWaitForCondWithService(gp->cond, timeout) == 0);
    mprResetCond(gp->cond);
    return rc;
}


void mprSignalTestComplete(MprTestGroup *gp)
{
    mprSignalCond(gp->cond);
}


static void adjustThreadCount(MprTestService *sp, int adj)
{
#if BLD_FEATURE_MULTITHREAD
    mprLock(sp->mutex);
    sp->activeThreadCount += adj;
    mprUnlock(sp->mutex);
#endif
}


static void adjustFailedCount(MprTestService *sp, int adj)
{
    mprLock(sp->mutex);
    sp->totalFailedCount += adj;
    mprUnlock(sp->mutex);
}


static void logHandler(MprCtx ctx, int flags, int level, cchar *msg)
{
    Mpr         *mpr;
    MprFile     *file;
    char        *prefix;

    mpr = mprGetMpr(ctx);
    file = (MprFile*) mpr->logHandlerData;
    prefix = mpr->name;

    while (*msg == '\n') {
        mprFprintf(file, "\n");
        msg++;
    }

    if (flags & MPR_LOG_SRC) {
        mprFprintf(file, "%s: %d: %s\n", prefix, level, msg);

    } else if (flags & MPR_ERROR_SRC) {
        /*
         *  Use static printing to avoid malloc when the messages are small.
         *  This is important for memory allocation errors.
         */
        if (strlen(msg) < (MPR_MAX_STRING - 32)) {
            mprStaticPrintf(file, "%s: Error: %s\n", prefix, msg);
        } else {
            mprFprintf(file, "%s: Error: %s\n", prefix, msg);
        }

    } else if (flags & MPR_FATAL_SRC) {
        mprFprintf(file, "%s: Fatal: %s\n", prefix, msg);
        
    } else if (flags & MPR_RAW) {
        mprFprintf(file, "%s", msg);
    }
    
    if (flags & (MPR_ERROR_SRC | MPR_FATAL_SRC)) {
        mprBreakpoint();
    }
}


static int setLogging(Mpr *mpr, char *logSpec)
{
    MprFile     *file;
    char        *levelSpec;
    int         level;

    level = 0;

    if ((levelSpec = strchr(logSpec, ':')) != 0) {
        *levelSpec++ = '\0';
        level = atoi(levelSpec);
    }

    if (strcmp(logSpec, "stdout") == 0) {
        file = mpr->fileSystem->stdOutput;

    } else if (strcmp(logSpec, "stderr") == 0) {
        file = mpr->fileSystem->stdError;

    } else {
        if ((file = mprOpen(mpr, logSpec, O_CREAT | O_WRONLY | O_TRUNC | O_TEXT, 0664)) == 0) {
            mprPrintfError(mpr, "Can't open log file %s\n", logSpec);
            return MPR_ERR_CANT_OPEN;
        }
    }

    mprSetLogLevel(mpr, level);
    mprSetLogHandler(mpr, logHandler, (void*) file);

    return 0;
}

#else
void __mprUnitTest() {}
#endif /* BLD_FEATURE_TEST */

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
