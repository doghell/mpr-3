/** 
 *  benchMpr.c - Benchmark various MPR facilities
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/********************************** Locals ************************************/

static int      iterations = 1;         /* Benchmark iterations */
static int      workers = 0;            /* Number of worker threads */

static MprCond  *complete;              /* Condition set when benchmark complete */
static int      markCount;              /* Flag set when benchmark complete */

#if BLD_FEATURE_MULTITHREAD
static MprMutex *mutex;                 /* Test synchronization */
#endif

/***************************** Forward Declarations ***************************/

static void     doBenchmark(Mpr *mpr, void *thread);
static void     endMark(MprCtx ctx, MprTime start, int count, char *msg);
static void     eventCallback(void *data, MprEvent *ep);
static MprTime  startMark(MprCtx ctx);
static void     timerCallback(void *data, MprEvent *ep);

volatile int    testComplete;

/*********************************** Code *************************************/
/*
 *  Initialize the library
 */

int benchMain(int argc, char *argv[])
{
    Mpr             *mpr;
    char            *argp;
    int             err, i, nextArg;

    mpr = mprCreate(argc, argv, 0);

#if VXWORKS || WINCE
    /*
     *  These platforms pass an arg string in via the argc value. Assumes 32-bit.
     */
    mprMakeArgv(mpr, "http", (char*) argc, &argc, &argv);
#endif

    err = 0;

    for (nextArg = 1; nextArg < argc; nextArg++) {
        argp = argv[nextArg];
        if (*argp != '-') {
            break;
        }

        if (strcmp(argp, "--iterations") == 0) {
            if (nextArg >= argc) {
                err++;
            } else {
                iterations = atoi(argv[++nextArg]);
            }

        } else if (strcmp(argp, "--workers") == 0 || strcmp(argp, "-w") == 0) {
            if (nextArg >= argc) {
                err++;
            } else {
                i = atoi(argv[++nextArg]);
                if (i <= 0 || i > 100) {
                    mprError(mpr, "%s: Bad number of worker threads (0-100)", mprGetAppName(mpr));
                    exit(2);
                
                }
                workers = i;
            }
        } else {
            err++;
        }
    }
    if (err) {
        mprPrintf(mpr, "usage: bench [-em] [-i iterations] [-t workers]\n");
        mprRawLog(mpr, 0,
            "usage: %s [options]\n"
            "    --iterations count  # Number of iterations to run the test\n"
            "    --workers count     # Set maximum worker threads\n",
            mprGetAppName(mpr));
        exit(2);
    }

#if BLD_FEATURE_MULTITHREAD
    mutex = mprCreateLock(mpr);
    mprSetMaxWorkers(mpr, workers);
#endif

    mprStart(mpr, 0);

#if BLD_FEATURE_MULTITHREAD
    mprCreateThread(mpr, "bench", (MprThreadProc) doBenchmark, (void*) mpr, MPR_NORMAL_PRIORITY, 0);
    while (!testComplete) {
        mprServiceEvents(mprGetDispatcher(mpr), 250, MPR_SERVICE_EVENTS | MPR_SERVICE_IO);
    }
#else
    doBenchmark(mpr, NULL);
#endif

    mprPrintf(mpr, "\n\n");
    // mprFree(mpr);
    return 0;
}


#if WINCE
int APIENTRY WinMain(HINSTANCE inst, HINSTANCE junk, LPWSTR args, int junk2)
{
    return benchMain((int) args, NULL);
}

#elif !VXWORKS
int main(int argc, char **argv)
{
    return benchMain(argc, argv);
}
#endif


/*
 *  Do a performance benchmark
 */ 
static void doBenchmark(Mpr *mpr, void *thread)
{
    MprEvent    *event;
    MprTime     start;
    MprList     *list;
    void        *mp;
    int         count, i;
#if BLD_FEATURE_MULTITHREAD
    MprMutex    *lock;
#endif

    complete = mprCreateCond(mpr);

    mprPrintf(mpr, "Group\t%-30s\t%13s\t%12s\n", "Benchmark", "Microsec", "Elapsed-sec");

    /*
     *  Malloc (1K)
     */
    mprPrintf(mpr, "Malloc Benchmarks\n");
    count = 2000000 * iterations;
    start = startMark(mpr);
    for (i = 0; i < count; i++) {
        mp = mprAlloc(mpr, 1024);
        mprFree(mp);
    }
    endMark(mpr, start, count, "Alloc mprAlloc(1K)|mprFree");
    start = startMark(mpr);

#if BLD_FEATURE_MULTITHREAD
    /*
     *  Locking primitives
     */
    mprPrintf(mpr, "Lock Benchmarks\n");
    lock = mprCreateLock(mpr);
    count = 5000000 * iterations;
    start = startMark(mpr);
    for (i = 0; i < count; i++) {
        mprLock(lock);
        mprUnlock(lock);
    }
    endMark(mpr, start, count, "Mutex lock|unlock");
    mprFree(lock);

    /*
     *  Condition signal / wait
     */
    mprPrintf(mpr, "Cond Benchmarks\n");
    count = 1000000 * iterations;
    start = startMark(mpr);
    mprResetCond(complete);
    for (i = 0; i < count; i++) {
        mprSignalCond(complete);
        mprWaitForCond(complete, -1);
    }
    endMark(mpr, start, count, "Cond signal|wait");
#endif

    /*
     *  List
     */
    mprPrintf(mpr, "List Benchmarks\n");
    count = 500000 * iterations;
    list = mprCreateList(mpr);
    start = startMark(mpr);
    for (i = 0; i < count; i++) {
        mprAddItem(list, (void*) (long) i);
        mprRemoveItem(list, (void*) (long) i);
    }
    endMark(mpr, start, count, "Link insert|remove");
    mprFree(list);;

    /*
     *  Events
     */
    mprPrintf(mpr, "Event Benchmarks\n");
    mprResetCond(complete);
    count = 200000 * iterations;
    markCount = count;
    start = startMark(mpr);
    for (i = 0; i < count; i++) {
        event = mprCreateEvent(mprGetDispatcher(mpr), eventCallback, 0, 0, (void*) (long) i, 0);
    }
    endMark(mpr, start, count, "Event (create)");
    mprWaitForCondWithService(complete, -1);
    endMark(mpr, start, count, "Event (run|delete)");


    /*
     *  Test timer creation, run and delete (make a million timers!)
     */
    mprPrintf(mpr, "Timer\n");
    mprResetCond(complete);
    count = 50000 * iterations;
    markCount = count;
    start = startMark(mpr);
    for (i = 0; i < count; i++) {
        mprCreateTimerEvent(mprGetDispatcher(mpr), timerCallback, 0, 0, (void*) (long) i, 0);
    }
    endMark(mpr, start, count, "Timer (create)");
    mprWaitForCondWithService(complete, -1);
    endMark(mpr, start, count, "Timer (delete)");

    testComplete = 1;
}


/*
 *  Event callback 
 */
static void eventCallback(void *data, MprEvent *event)
{
    mprLock(mutex);
    if (--markCount == 0) {
        mprSignalCond(complete);
    }
    mprStopContinuousEvent(event);
    mprUnlock(mutex);
}


/*
 *  Timer callback 
 */
static void timerCallback(void *data, MprEvent *event)
{
    mprLock(mutex);
    if (--markCount == 0) {
        mprSignalCond(complete);
    }
    mprStopContinuousEvent(event);
    mprUnlock(mutex);
}


static MprTime startMark(MprCtx ctx)
{
    MprTime     now;

    now = mprGetTime(ctx);
    return now;
}


static void endMark(MprCtx ctx, MprTime start, int count, char *msg)
{
    MprTime     elapsed;

    elapsed = mprGetRemainingTime(ctx, start, 0);
    mprPrintf(ctx, "\t%-30s\t%13.2f\t%12.2f\n", 
        msg, elapsed * 1000.0 / count, elapsed / 1000.0);
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
