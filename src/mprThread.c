/**
 *  mprThread.c - Primitive multi-threading support for Windows
 *
 *  This module provides threading, mutex and condition variable APIs.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes **********************************/

#include    "mpr.h"

#if BLD_FEATURE_MULTITHREAD

/*************************** Forward Declarations ****************************/

static int  changeState(MprWorker *worker, int state);
static MprWorker *createWorker(MprWorkerService *ws, int stackSize);
static int  getNextThreadNum(MprWorkerService *ws);
static int  workerDestructor(MprWorker *worker);
static void pruneWorkers(MprWorkerService *ws, MprEvent *timer);
static void threadProc(MprThread *tp);
static int threadDestructor(MprThread *tp);
static void workerMain(MprWorker *worker, MprThread *tp);

/************************************ Code ***********************************/

MprThreadService *mprCreateThreadService(Mpr *mpr)
{
    MprThreadService    *ts;

    mprAssert(mpr);

    ts = mprAllocObjZeroed(mpr, MprThreadService);
    if (ts == 0) {
        return 0;
    }
    ts->mutex = mprCreateLock(mpr);
    if (ts->mutex == 0) {
        mprFree(ts);
        return 0;
    }
    ts->threads = mprCreateList(ts);
    if (ts->threads == 0) {
        mprFree(ts);
        return 0;
    }
    mpr->serviceThread = mpr->mainOsThread = mprGetCurrentOsThread();
    mpr->threadService = ts;
    ts->stackSize = MPR_DEFAULT_STACK;

    /*
     *  Don't actually create the thread. Just create a thread object for this main thread.
     */
    ts->mainThread = mprCreateThread(ts, "main", 0, 0, MPR_NORMAL_PRIORITY, 0);
    if (ts->mainThread == 0) {
        mprFree(ts);
        return 0;
    }
    ts->mainThread->isMain = 1;
    return ts;
}


bool mprStopThreadService(MprThreadService *ts, int timeout)
{
    while (timeout > 0 && ts->threads->length > 1) {
        mprSleep(ts, 50);
        timeout -= 50;
    }
    return ts->threads->length == 0;
}


void mprSetThreadStackSize(MprCtx ctx, int size)
{
    mprGetMpr(ctx)->threadService->stackSize = size;
}


/*
 *  Return the current thread object
 */
MprThread *mprGetCurrentThread(MprCtx ctx)
{
    MprThreadService    *ts;
    MprThread           *tp;
    MprOsThread         id;
    int                 i;

    ts = mprGetMpr(ctx)->threadService;
    mprLock(ts->mutex);
    id = mprGetCurrentOsThread();
    for (i = 0; i < ts->threads->length; i++) {
        tp = (MprThread*) mprGetItem(ts->threads, i);
        if (tp->osThread == id) {
            mprUnlock(ts->mutex);
            return tp;
        }
    }
    mprUnlock(ts->mutex);
    return 0;
}


/*
 *  Return the current thread object
 */
cchar *mprGetCurrentThreadName(MprCtx ctx)
{
    MprThread       *tp;

    tp = mprGetCurrentThread(ctx);
    if (tp == 0) {
        return 0;
    }
    return tp->name;
}


/*
 *  Return the current thread object
 */
void mprSetCurrentThreadPriority(MprCtx ctx, int pri)
{
    MprThread       *tp;

    tp = mprGetCurrentThread(ctx);
    if (tp == 0) {
        return;
    }
    mprSetThreadPriority(tp, pri);
}


/*
 *  Create a main thread
 */
MprThread *mprCreateThread(MprCtx ctx, cchar *name, MprThreadProc entry, void *data, int priority, int stackSize)
{
    MprThreadService    *ts;
    MprThread           *tp;

    ts = mprGetMpr(ctx)->threadService;
    if (ts) {
        ctx = ts;
    }
    tp = mprAllocObjWithDestructorZeroed(ctx, MprThread, threadDestructor);
    if (tp == 0) {
        return 0;
    }
    tp->data = data;
    tp->entry = entry;
    tp->name = mprStrdup(tp, name);
    tp->mutex = mprCreateLock(tp);
    tp->pid = getpid();
    tp->priority = priority;

    if (stackSize == 0) {
        tp->stackSize = ts->stackSize;
    } else {
        tp->stackSize = stackSize;
    }
#if BLD_WIN_LIKE
    tp->threadHandle = 0;
#endif
    if (ts && ts->threads) {
        mprLock(ts->mutex);
        if (mprAddItem(ts->threads, tp) < 0) {
            mprFree(tp);
            mprUnlock(ts->mutex);
            return 0;
        }
        mprUnlock(ts->mutex);
    }
    return tp;
}


/*
 *  Destroy a thread
 */
static int threadDestructor(MprThread *tp)
{
    MprThreadService    *ts;

    mprLock(tp->mutex);

    ts = mprGetMpr(tp)->threadService;
    mprRemoveItem(ts->threads, tp);

#if BLD_WIN_LIKE
    if (tp->threadHandle) {
        CloseHandle(tp->threadHandle);
    }
#endif
    return 0;
}


/*
 *  Entry thread function
 */ 
#if BLD_WIN_LIKE
static uint __stdcall threadProcWrapper(void *data) 
{
    threadProc((MprThread*) data);
    return 0;
}
#elif VXWORKS

static int threadProcWrapper(void *data) 
{
    threadProc((MprThread*) data);
    return 0;
}

#else
void *threadProcWrapper(void *data) 
{
    threadProc((MprThread*) data);
    return 0;
}

#endif


/*
 *  Thread entry
 */
static void threadProc(MprThread *tp)
{
    mprAssert(tp);

    tp->osThread = mprGetCurrentOsThread();

#if VXWORKS
    tp->pid = tp->osThread;
#else
    tp->pid = getpid();
#endif
    (tp->entry)(tp->data, tp);
    mprFree(tp);
}


/*
 *  Start a thread
 */
int mprStartThread(MprThread *tp)
{
    mprLock(tp->mutex);

#if BLD_WIN_LIKE
{
    HANDLE          h;
    uint            threadId;

#if WINCE
    h = (HANDLE) CreateThread(NULL, 0, threadProcWrapper, (void*) tp, 0, &threadId);
#else
    h = (HANDLE) _beginthreadex(NULL, 0, threadProcWrapper, (void*) tp, 0, &threadId);
#endif
    if (h == NULL) {
        return MPR_ERR_CANT_INITIALIZE;
    }
    tp->osThread = (int) threadId;
    tp->threadHandle = (HANDLE) h;
}
#elif VXWORKS
{
    int     taskHandle, pri;

    taskPriorityGet(taskIdSelf(), &pri);
    taskHandle = taskSpawn(tp->name, pri, 0, tp->stackSize, (FUNCPTR) threadProcWrapper, (int) tp, 
        0, 0, 0, 0, 0, 0, 0, 0, 0);

    if (taskHandle < 0) {
        mprError(tp, "Can't create thread %s\n", tp->name);
        return MPR_ERR_CANT_INITIALIZE;
    }
}
#else /* UNIX */
{
    pthread_attr_t  attr;
    pthread_t       h;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, tp->stackSize);

    if (pthread_create(&h, &attr, threadProcWrapper, (void*) tp) != 0) { 
        mprAssert(0);
        pthread_attr_destroy(&attr);
        return MPR_ERR_CANT_CREATE;
    }
    pthread_attr_destroy(&attr);
}
#endif
    mprSetThreadPriority(tp, tp->priority);
    mprUnlock(tp->mutex);
    return 0;
}


MprOsThread mprGetCurrentOsThread()
{
#if BLD_UNIX_LIKE
    return (MprOsThread) pthread_self();
#elif BLD_WIN_LIKE
    return (MprOsThread) GetCurrentThreadId();
#elif VXWORKS
    return (MprOsThread) taskIdSelf();
#endif
}


void mprSetThreadPriority(MprThread *tp, int newPriority)
{
    int     osPri;

    mprLock(tp->mutex);

    osPri = mprMapMprPriorityToOs(newPriority);

#if BLD_WIN_LIKE
    SetThreadPriority(tp->threadHandle, osPri);
#elif VXWORKS
    taskPrioritySet(tp->osThread, osPri);
#else
    setpriority(PRIO_PROCESS, tp->pid, osPri);
#endif
    tp->priority = newPriority;
    mprUnlock(tp->mutex);
}


static int threadLocalDestructor(MprThreadLocal *tls)
{
#if BLD_UNIX_LIKE
    if (tls->key) {
        pthread_key_delete(tls->key);
    }
#elif BLD_WIN_LIKE
    if (tls->key >= 0) {
        TlsFree(tls->key);
    }
#endif
    return 0;
}


MprThreadLocal *mprCreateThreadLocal(MprCtx ctx)
{
    MprThreadLocal      *tls;

    tls = mprAllocObjWithDestructorZeroed(ctx, MprThreadLocal, threadLocalDestructor);
    if (tls == 0) {
        return 0;
    }
#if BLD_UNIX_LIKE
    if (pthread_key_create(&tls->key, NULL) != 0) {
        tls->key = 0;
        mprFree(tls);
        return 0;
    }
#elif BLD_WIN_LIKE
    if ((tls->key = TlsAlloc()) < 0) {
        return 0;
    }
#endif
    return tls;
}


int mprSetThreadData(MprThreadLocal *tls, void *value)
{
    bool    err;

    err = 1;
#if BLD_UNIX_LIKE
    err = pthread_setspecific(tls->key, value) != 0;
#elif BLD_WIN_LIKE
    err = TlsSetValue(tls->key, value) != 0;
#endif
    return (err) ? MPR_ERR_CANT_WRITE: 0;
}


void *mprGetThreadData(MprThreadLocal *tls)
{
#if BLD_UNIX_LIKE
    return pthread_getspecific(tls->key);
#elif BLD_WIN_LIKE
    return TlsGetValue(tls->key);
#elif VXWORKS
    /* Not supported */
    return 0;
#endif
}


#if BLD_WIN_LIKE
/*
 *  Map Mpr priority to Windows native priority. Windows priorities range from -15 to +15 (zero is normal). 
 *  Warning: +15 will not yield the CPU, -15 may get starved. We should be very wary going above +11.
 */

int mprMapMprPriorityToOs(int mprPriority)
{
    mprAssert(mprPriority >= 0 && mprPriority <= 100);
 
    if (mprPriority <= MPR_BACKGROUND_PRIORITY) {
        return THREAD_PRIORITY_LOWEST;
    } else if (mprPriority <= MPR_LOW_PRIORITY) {
        return THREAD_PRIORITY_BELOW_NORMAL;
    } else if (mprPriority <= MPR_NORMAL_PRIORITY) {
        return THREAD_PRIORITY_NORMAL;
    } else if (mprPriority <= MPR_HIGH_PRIORITY) {
        return THREAD_PRIORITY_ABOVE_NORMAL;
    } else {
        return THREAD_PRIORITY_HIGHEST;
    }
}


/*
 *  Map Windows priority to Mpr priority
 */ 
int mprMapOsPriorityToMpr(int nativePriority)
{
    int     priority;

    priority = (45 * nativePriority) + 50;
    if (priority < 0) {
        priority = 0;
    }
    if (priority >= 100) {
        priority = 99;
    }
    return priority;
}


#elif VXWORKS
/*
 *  Map MPR priority to VxWorks native priority.
 */

int mprMapMprPriorityToOs(int mprPriority)
{
    int     nativePriority;

    mprAssert(mprPriority >= 0 && mprPriority < 100);

    nativePriority = (100 - mprPriority) * 5 / 2;

    if (nativePriority < 10) {
        nativePriority = 10;
    } else if (nativePriority > 255) {
        nativePriority = 255;
    }
    return nativePriority;
}


/*
 *  Map O/S priority to Mpr priority.
 */ 
int mprMapOsPriorityToMpr(int nativePriority)
{
    int     priority;

    priority = (255 - nativePriority) * 2 / 5;
    if (priority < 0) {
        priority = 0;
    }
    if (priority >= 100) {
        priority = 99;
    }
    return priority;
}


#else /* UNIX */
/*
 *  Map MR priority to linux native priority. Unix priorities range from -19 to +19. Linux does -20 to +19. 
 */
int mprMapMprPriorityToOs(int mprPriority)
{
    mprAssert(mprPriority >= 0 && mprPriority < 100);

    if (mprPriority <= MPR_BACKGROUND_PRIORITY) {
        return 19;
    } else if (mprPriority <= MPR_LOW_PRIORITY) {
        return 10;
    } else if (mprPriority <= MPR_NORMAL_PRIORITY) {
        return 0;
    } else if (mprPriority <= MPR_HIGH_PRIORITY) {
        return -8;
    } else {
        return -19;
    }
    mprAssert(0);
    return 0;
}


/*
 *  Map O/S priority to Mpr priority.
 */ 
int mprMapOsPriorityToMpr(int nativePriority)
{
    int     priority;

    priority = (nativePriority + 19) * (100 / 40); 
    if (priority < 0) {
        priority = 0;
    }
    if (priority >= 100) {
        priority = 99;
    }
    return priority;
}

#endif /* UNIX */


MprWorkerService *mprCreateWorkerService(MprCtx ctx)
{
    MprWorkerService      *ws;

    ws = mprAllocObjZeroed(ctx, MprWorkerService);
    if (ws == 0) {
        return 0;
    }
    ws->mutex = mprCreateLock(ws);
    ws->minThreads = MPR_DEFAULT_MIN_THREADS;
    ws->maxThreads = MPR_DEFAULT_MAX_THREADS;

    /*
     *  Presize the lists so they cannot get memory allocation failures later on.
     */
    ws->idleThreads = mprCreateList(ws);
    mprSetListLimits(ws->idleThreads, ws->maxThreads, -1);

    ws->busyThreads = mprCreateList(ws);
    mprSetListLimits(ws->busyThreads, ws->maxThreads, -1);
    return ws;
}


int mprStartWorkerService(MprWorkerService *ws)
{
    /*
     *  Create a timer to trim excess threads in the worker
     */
    mprSetMinWorkers(ws, ws->minThreads);
    ws->pruneTimer = mprCreateTimerEvent(mprGetDispatcher(ws), (MprEventProc) pruneWorkers, MPR_TIMEOUT_PRUNER, 
        MPR_NORMAL_PRIORITY, (void*) ws, 0);
    return 0;
}


bool mprStopWorkerService(MprWorkerService *ws, int timeout)
{
    MprWorker     *worker;
    int           rc, next;

    rc = 0;
    mprLock(ws->mutex);

    if (ws->pruneTimer) {
        mprFree(ws->pruneTimer);
        ws->pruneTimer = 0;
    }

    /*
     *  Wake up all idle threads. Busy threads take care of themselves. An idle thread will wakeup, exit and be 
     *  removed from the busy list and then delete the thread. We progressively remove the last thread in the idle
     *  list. ChangeState will move the threads to the busy queue.
     */
    for (next = -1; (worker = (MprWorker*) mprGetPrevItem(ws->idleThreads, &next)) != 0; ) {
        changeState(worker, MPR_WORKER_BUSY);
    }

    /*
     *  Wait until all tasks and threads have exited
     */
    while (timeout > 0 && ws->numThreads > 0) {
        mprUnlock(ws->mutex);
        mprSleep(ws, 50);
        timeout -= 50;
        mprLock(ws->mutex);
    }

    mprAssert(ws->idleThreads->length == 0);
    mprAssert(ws->busyThreads->length == 0);
    mprUnlock(ws->mutex);
    return ws->numThreads == 0;
}


/*
 *  Define the new minimum number of threads. Pre-allocate the minimum.
 */
void mprSetMinWorkers(MprCtx ctx, int n)
{ 
    MprWorker           *worker;
    MprWorkerService    *ws;

    ws = mprGetMpr(ctx)->workerService;

    mprLock(ws->mutex);

    ws->minThreads = n; 
    while (ws->numThreads < ws->minThreads) {
        worker = createWorker(ws, ws->stackSize);
        ws->numThreads++;
        ws->maxUseThreads = max(ws->numThreads, ws->maxUseThreads);
        ws->pruneHighWater = max(ws->numThreads, ws->pruneHighWater);
        changeState(worker, MPR_WORKER_BUSY);
        mprStartThread(worker->thread);
    }
    mprUnlock(ws->mutex);
}


/*
 *  Define a new maximum number of theads. Prune if currently over the max.
 */
void mprSetMaxWorkers(MprCtx ctx, int n)
{
    MprWorkerService  *ws;

    ws = mprGetMpr(ctx)->workerService;

    mprLock(ws->mutex);
    ws->maxThreads = n; 
    if (ws->numThreads > ws->maxThreads) {
        pruneWorkers(ws, 0);
    }
    if (ws->minThreads > ws->maxThreads) {
        ws->minThreads = ws->maxThreads;
    }
    mprUnlock(ws->mutex);
}


int mprGetMaxWorkers(MprCtx ctx)
{
    return mprGetMpr(ctx)->workerService->maxThreads;
}


/*
 *  Return the current worker thread object
 */
MprWorker *mprGetCurrentWorker(MprCtx ctx)
{
    MprWorkerService    *ws;
    MprWorker           *worker;
    MprThread           *thread;
    int                 next;

    ws = mprGetMpr(ctx)->workerService;

    mprLock(ws->mutex);
    thread = mprGetCurrentThread(ws);
    for (next = -1; (worker = (MprWorker*) mprGetPrevItem(ws->busyThreads, &next)) != 0; ) {
        if (worker->thread == thread) {
            mprUnlock(ws->mutex);
            return worker;
        }
    }
    mprUnlock(ws->mutex);
    return 0;
}


/*
 *  Set the worker as dedicated to the current task
 */
void mprDedicateWorker(MprWorker *worker)
{
    mprLock(worker->workerService->mutex);
    worker->flags |= MPR_WORKER_DEDICATED;
    mprUnlock(worker->workerService->mutex);
}


void mprReleaseWorker(MprWorker *worker)
{
    mprLock(worker->workerService->mutex);
    worker->flags &= ~MPR_WORKER_DEDICATED;
    mprUnlock(worker->workerService->mutex);
}


void mprActivateWorker(MprWorker *worker, MprWorkerProc proc, void *data, int priority)
{
    MprWorkerService    *ws;

    ws = worker->workerService;

    mprLock(ws->mutex);
    worker->proc = proc;
    worker->data = data;
    worker->priority = priority;
    mprAssert(worker->flags & MPR_WORKER_DEDICATED);
    changeState(worker, MPR_WORKER_BUSY);
    mprUnlock(ws->mutex);
}


int mprStartWorker(MprCtx ctx, MprWorkerProc proc, void *data, int priority)
{
    MprWorkerService    *ws;
    MprWorker           *worker;
    int                 next;

    ws = mprGetMpr(ctx)->workerService;

    mprLock(ws->mutex);

    /*
     *  Try to find an idle thread and wake it up. It will wakeup in workerMain(). If not any available, then add 
     *  another thread to the worker. Must account for threads we've already created but have not yet gone to work 
     *  and inserted themselves in the idle/busy queues.
     */
    for (next = 0; (worker = (MprWorker*) mprGetNextItem(ws->idleThreads, &next)) != 0; ) {
        if (!(worker->flags & MPR_WORKER_DEDICATED)) {
            break;
        }
    }

    if (worker) {
        worker->proc = proc;
        worker->data = data;
        worker->priority = priority;
        changeState(worker, MPR_WORKER_BUSY);

    } else if (ws->numThreads < ws->maxThreads) {

        /*
         *  Can't find an idle thread. Try to create more threads in the worker. Otherwise, we will have to wait. 
         *  No need to wakeup the thread -- it will immediately go to work.
         */
        worker = createWorker(ws, ws->stackSize);

        ws->numThreads++;
        ws->maxUseThreads = max(ws->numThreads, ws->maxUseThreads);
        ws->pruneHighWater = max(ws->numThreads, ws->pruneHighWater);

        worker->proc = proc;
        worker->data = data;
        worker->priority = priority;

        changeState(worker, MPR_WORKER_BUSY);
        mprStartThread(worker->thread);

    } else {
        static int warned = 0;
        /*
         *  No free threads and can't create anymore
         */
        if (warned++ == 0) {
            mprError(ctx, "No free worker threads, using service thread. (currently allocated %d)", ws->numThreads);
        }
        mprUnlock(ws->mutex);
        return MPR_ERR_BUSY;
    }
    mprUnlock(ws->mutex);
    return 0;
}


/*
 *  Trim idle threads from a task
 */
static void pruneWorkers(MprWorkerService *ws, MprEvent *timer)
{
    MprWorker     *worker;
    int           index, toTrim;

    if (mprGetDebugMode(ws)) {
        return;
    }
    /*
     *  Prune half of what we could prune. This gives exponentional decay. We use the high water mark seen in 
     *  the last period.
     */
    mprLock(ws->mutex);
    toTrim = (ws->pruneHighWater - ws->minThreads) / 2;

    for (index = 0; toTrim-- > 0 && index < ws->idleThreads->length; index++) {
        worker = (MprWorker*) mprGetItem(ws->idleThreads, index);
        /*
         *  Leave floating -- in no queue. The thread will kill itself.
         */
        changeState(worker, MPR_WORKER_PRUNED);
    }
    ws->pruneHighWater = ws->minThreads;
    mprUnlock(ws->mutex);
}


int mprGetAvailableWorkers(MprCtx ctx)
{
    MprWorkerService  *ws;

    ws = mprGetMpr(ctx)->workerService;
    return ws->idleThreads->length + (ws->maxThreads - ws->numThreads); 
}


static int getNextThreadNum(MprWorkerService *ws)
{
    int     rc;

    mprLock(ws->mutex);
    rc = ws->nextThreadNum++;
    mprUnlock(ws->mutex);
    return rc;
}


/*
 *  Define a new stack size for new threads. Existing threads unaffected.
 */
void mprSetWorkerStackSize(MprCtx ctx, int n)
{
    MprWorkerService  *ws;

    ws = mprGetMpr(ctx)->workerService;
    ws->stackSize = n; 
}


void mprGetWorkerServiceStats(MprWorkerService *ws, MprWorkerStats *stats)
{
    mprAssert(ws);

    stats->maxThreads = ws->maxThreads;
    stats->minThreads = ws->minThreads;
    stats->numThreads = ws->numThreads;
    stats->maxUse = ws->maxUseThreads;
    stats->pruneHighWater = ws->pruneHighWater;
    stats->idleThreads = ws->idleThreads->length;
    stats->busyThreads = ws->busyThreads->length;
}


/*
 *  Create a new thread for the task
 */
static MprWorker *createWorker(MprWorkerService *ws, int stackSize)
{
    MprWorker   *worker;

    char    name[16];

    worker = mprAllocObjWithDestructorZeroed(ws, MprWorker, workerDestructor);
    if (worker == 0) {
        return 0;
    }

    worker->flags = 0;
    worker->proc = 0;
    worker->cleanup = 0;
    worker->data = 0;
    worker->priority = 0;
    worker->state = 0;
    worker->workerService = ws;
    worker->idleCond = mprCreateCond(worker);

    mprSprintf(name, sizeof(name), "worker.%u", getNextThreadNum(ws));
    worker->thread = mprCreateThread(ws, name, (MprThreadProc) workerMain, (void*) worker, MPR_WORKER_PRIORITY, 0);
    return worker;
}


static int workerDestructor(MprWorker *worker)
{
    if (worker->thread != 0) {
        mprAssert(worker->thread);
        return 1;
    }
    return 0;
}


/*
 *  Worker thread main service routine
 */
static void workerMain(MprWorker *worker, MprThread *tp)
{
    MprWorkerService    *ws;
    int                 rc;

    ws = mprGetMpr(worker)->workerService;
    mprAssert(worker->state == MPR_WORKER_BUSY);
    mprAssert(!worker->idleCond->triggered);

    mprLock(ws->mutex);

    while (!(worker->state & MPR_WORKER_PRUNED)) {
        if (worker->proc) {
            mprUnlock(ws->mutex);
            mprSetThreadPriority(worker->thread, worker->priority);

            (*worker->proc)(worker->data, worker);

            mprLock(ws->mutex);
            worker->proc = 0;
            mprSetThreadPriority(worker->thread, MPR_WORKER_PRIORITY);
        }
        changeState(worker, MPR_WORKER_SLEEPING);

        if (worker->cleanup) {
            (*worker->cleanup)(worker->data, worker);
            worker->cleanup = NULL;
        }
        mprUnlock(ws->mutex);

        /*
         *  Sleep till there is more work to do
         */
        rc = mprWaitForCond(worker->idleCond, -1);

        mprLock(ws->mutex);
        mprAssert(worker->state == MPR_WORKER_BUSY || worker->state == MPR_WORKER_PRUNED);
    }

    changeState(worker, 0);

    ws->numThreads--;
    worker->thread = 0;
    mprUnlock(ws->mutex);
}


static int changeState(MprWorker *worker, int state)
{
    MprWorkerService    *ws;
    MprList             *lp;

    mprAssert(worker->state != state);

    ws = worker->workerService;

    lp = 0;
    mprLock(ws->mutex);
    switch (worker->state) {
    case MPR_WORKER_BUSY:
        lp = ws->busyThreads;
        break;

    case MPR_WORKER_IDLE:
        lp = ws->idleThreads;
        break;

    case MPR_WORKER_SLEEPING:
        if (!(worker->flags & MPR_WORKER_DEDICATED)) {
            lp = ws->idleThreads;
        }
        mprSignalCond(worker->idleCond); 
        break;
        
    case MPR_WORKER_PRUNED:
        break;
    }

    /*
     *  Reassign the worker to the appropriate queue
     */
    if (lp) {
        mprRemoveItem(lp, worker);
    }
    lp = 0;
    switch (state) {
    case MPR_WORKER_BUSY:
        lp = ws->busyThreads;
        break;

    case MPR_WORKER_IDLE:
    case MPR_WORKER_SLEEPING:
        if (!(worker->flags & MPR_WORKER_DEDICATED)) {
            lp = ws->idleThreads;
        }
        break;

    case MPR_WORKER_PRUNED:
        /* Don't put on a queue and the thread will exit */
        break;
    }
    
    worker->state = state;

    if (lp) {
        if (mprAddItem(lp, worker) < 0) {
            mprUnlock(ws->mutex);
            return MPR_ERR_NO_MEMORY;
        }
    }
    mprUnlock(ws->mutex);
    return 0;
}


#else
cchar *mprGetCurrentThreadName(MprCtx ctx) { return "main"; }
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
