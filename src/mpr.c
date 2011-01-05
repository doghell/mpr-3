/**
 *  mpr.c - Michael's Portable Runtime (MPR). Initialization, start/stop and control of the MPR.
 *
 *  Copyright (c) All Rights Reserved. See copyright notice at the bottom of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/**************************** Forward Declarations ****************************/

static void memoryFailure(MprCtx ctx, int64 size, int64 total, bool granted);
static int  mprDestructor(Mpr *mpr);

#if BLD_FEATURE_MULTITHREAD
static void serviceEvents(void *data, MprThread *tp);
#endif

/************************************* Code ***********************************/
/*
 *  Create the MPR service. This routine is the first call an MPR application must do. It creates the top 
 *  level memory context.
 */

Mpr *mprCreate(int argc, char **argv, MprAllocNotifier cback)
{
    return mprCreateEx(argc, argv, cback, NULL);
}


/*
 *  Add a shell parameter then do the regular init
 */
Mpr *mprCreateEx(int argc, char **argv, MprAllocNotifier cback, void *shell)
{
    MprFileSystem   *fs;
    Mpr             *mpr;
    char            *cp;

    if (cback == 0) {
        cback = memoryFailure;
    }
    mpr = (Mpr*) mprCreateAllocService(cback, (MprDestructor) mprDestructor);

    if (mpr == 0) {
        mprAssert(mpr);
        return 0;
    }
    
    /*
     *  Wince and Vxworks passes an arg via argc, and the program name in argv. NOTE: this will only work on 32-bit systems.
     */
#if WINCE
    mprMakeArgv(mpr, (char*) argv, mprToAsc(mpr, (uni*) argc), &argc, &argv);
#elif VXWORKS
    mprMakeArgv(mpr, NULL, (char*) argc, &argc, &argv);
#endif
    mpr->argc = argc;
    mpr->argv = argv;

    mpr->name = mprStrdup(mpr, BLD_PRODUCT);
    mpr->title = mprStrdup(mpr, BLD_NAME);
    mpr->version = mprStrdup(mpr, BLD_VERSION);
    mpr->idleCallback = mprServicesAreIdle;

    if (mprCreateTimeService(mpr) < 0) {
        goto error;
    }
    if ((mpr->osService = mprCreateOsService(mpr)) < 0) {
        goto error;
    }

    /*
     *  See if any of the preceeding allocations failed and mark all blocks allocated so far as required.
     *  They will then be omitted from leak reports.
     */
    if (mprHasAllocError(mpr)) {
        goto error;
    }

#if BREW
    mprSetShell(mpr, shell);
#endif

#if BLD_FEATURE_MULTITHREAD
    mpr->multiThread = 1;
    if ((mpr->threadService = mprCreateThreadService(mpr)) == 0) {
        goto error;
    }
    mpr->mutex = mprCreateLock(mpr);
    mpr->spin = mprCreateSpinLock(mpr);
#endif

    if ((fs = mprCreateFileSystem(mpr, "/")) == 0) {
        goto error;
    }
    mprAddFileSystem(mpr, fs);

    if ((mpr->moduleService = mprCreateModuleService(mpr)) == 0) {
        goto error;
    }
    if ((mpr->dispatcher = mprCreateDispatcher(mpr)) == 0) {
        goto error;
    }
    if ((mpr->cmdService = mprCreateCmdService(mpr)) == 0) {
        goto error;
    }
#if BLD_FEATURE_MULTITHREAD
    if ((mpr->workerService = mprCreateWorkerService(mpr)) == 0) {
        goto error;
    }
#endif
    if ((mpr->waitService = mprCreateWaitService(mpr)) == 0) {
        goto error;
    }
    if ((mpr->socketService = mprCreateSocketService(mpr)) == 0) {
        goto error;
    }
#if BLD_FEATURE_HTTP
    if ((mpr->httpService = mprCreateHttpService(mpr)) == 0) {
        goto error;
    }
#endif

    if (mpr->argv && mpr->argv[0] && *mpr->argv[0]) {
        mprFree(mpr->name);
        mpr->name = mprGetPathBase(mpr, mpr->argv[0]);
        if ((cp = strchr(mpr->name, '.')) != 0) {
            *cp = '\0';
        }
    }

    /*
     *  Now catch all memory allocation errors up to this point. Should be none.
     */
    if (mprHasAllocError(mpr)) {
        goto error;
    }
    return mpr;

/*
 *  Error return
 */
error:
    mprFree(mpr);
    return 0;
}


static int mprDestructor(Mpr *mpr)
{
    if ((mpr->flags & MPR_STARTED) && !(mpr->flags & MPR_STOPPED)) {
        if (!mprStop(mpr)) {
            return 1;
        }
    }
    return 0;

}


/*
 *  Start the Mpr and all services
 */
int mprStart(Mpr *mpr, int startEventsThread)
{
    int     rc;

    rc = mprStartOsService(mpr->osService);
    rc += mprStartModuleService(mpr->moduleService);
#if BLD_FEATURE_MULTITHREAD
    rc += mprStartWorkerService(mpr->workerService);
#endif
    rc += mprStartSocketService(mpr->socketService);
#if BLD_FEATURE_HTTP
    rc += mprStartHttpService(mpr->httpService);
#endif

    if (rc != 0) {
        mprUserError(mpr, "Can't start MPR services");
        return MPR_ERR_CANT_INITIALIZE;
    }
    mpr->flags |= MPR_STARTED;
    mprLog(mpr, MPR_INFO, "MPR services are ready");
#if BLD_FEATURE_MULTITHREAD
    if (startEventsThread) {
        mprStartEventsThread(mpr);
    }
#endif
    return 0;
}


bool mprStop(Mpr *mpr)
{
    int     stopped;

    stopped = 1;

    mprLock(mpr->mutex);
    if (! (mpr->flags & MPR_STARTED) || (mpr->flags & MPR_STOPPED)) {
        mprUnlock(mpr->mutex);
        return 0;
    }
    mpr->flags |= MPR_STOPPED;

    /*
        Trigger graceful termination. This will prevent further tasks and events being created.
     */
    mprTerminate(mpr, 1);

#if BLD_FEATURE_HTTP
    mprStopHttpService(mpr->httpService);
#endif
    mprStopSocketService(mpr->socketService);
#if BLD_FEATURE_MULTITHREAD
    if (!mprStopWorkerService(mpr->workerService, MPR_TIMEOUT_STOP_TASK)) {
        stopped = 0;
    }
    if (!mprStopThreadService(mpr->threadService, MPR_TIMEOUT_STOP_TASK)) {
        stopped = 0;
    }
#endif
    mprStopModuleService(mpr->moduleService);
    mprStopOsService(mpr->osService);
    return stopped;
}


#if BLD_FEATURE_MULTITHREAD
/*
 *  Thread to service the event queue. Used if the user does not have their own main event loop.
 */
int mprStartEventsThread(Mpr *mpr)
{
    MprThread   *tp;

    mprLog(mpr, MPR_CONFIG, "Starting service thread");

    if ((tp = mprCreateThread(mpr, "events", serviceEvents, 0, MPR_NORMAL_PRIORITY, 0)) == 0) {
        return MPR_ERR_CANT_CREATE;
    }
    mpr->hasDedicatedService = 1;
    mprStartThread(tp);
    return 0;
}


/*
 *  Thread main for serviceEvents
 */
static void serviceEvents(void *data, MprThread *tp)
{
    Mpr     *mpr;

    mpr = mprGetMpr(tp);
    mpr->serviceThread = tp->osThread;
    mprServiceEvents(mpr->dispatcher, -1, MPR_SERVICE_EVENTS | MPR_SERVICE_IO);
    mpr->serviceThread = 0;
    mpr->hasDedicatedService = 1;
}


void mprSetServiceThread(MprCtx ctx, MprThread *thread)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    mpr->serviceThread = thread->osThread;
}


/*
 *  If this thread is not responsibile for running the Mpr dispatcher then return true.
 */
bool mprMustWakeDispatcher(MprCtx ctx)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);

#if BLD_FEATURE_MULTITHREAD
    return mprGetCurrentOsThread() != mpr->serviceThread;
#else
    return 0;
#endif
}
#endif /* BLD_FEATURE_MULTITHREAD */


/*
 *  Exit the mpr gracefully. Instruct the event loop to exit.
 */
void mprTerminate(MprCtx ctx, bool graceful)
{
    if (! graceful) {
        exit(0);
    }
    mprSignalExit(ctx);
}


bool mprIsExiting(MprCtx ctx)
{
    Mpr *mpr;

    mpr = mprGetMpr(ctx);
    if (mpr == 0) {
        return 1;
    }
    return mpr->flags & MPR_EXITING;
}


bool mprIsComplete(MprCtx ctx)
{
    Mpr *mpr;

    mpr = mprGetMpr(ctx);
    if (mpr == 0) {
        return 1;
    }
    return (mpr->flags & MPR_EXITING) && mprIsIdle(ctx);
}


/*
    Just the Mpr services are idle. Use mprIsIdle to determine if the entire process is idle
 */
bool mprServicesAreIdle(MprCtx ctx)
{
    Mpr     *mpr;
    
    mpr = mprGetMpr(ctx);
#if BLD_FEATURE_MULTITHREAD
    return mprGetListCount(mpr->workerService->busyThreads) == 0 && mprGetListCount(mpr->cmdService->cmds) == 0 && 
       !(mpr->dispatcher->flags & MPR_DISPATCHER_DO_EVENT);
#else
    return mprGetListCount(mpr->cmdService->cmds) == 0 && !(mpr->dispatcher->flags & MPR_DISPATCHER_DO_EVENT);
#endif
}


bool mprIsIdle(MprCtx ctx)
{
    return (mprGetMpr(ctx)->idleCallback)(ctx);
}


MprIdleCallback mprSetIdleCallback(MprCtx ctx, MprIdleCallback idleCallback)
{
    MprIdleCallback old;
    Mpr             *mpr;
    
    mpr = mprGetMpr(ctx);
    old = mpr->idleCallback;
    mpr->idleCallback = idleCallback;
    return old;
}


void mprSignalExit(MprCtx ctx)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);

    mprSpinLock(mpr->spin);
    mpr->flags |= MPR_EXITING;
    mprSpinUnlock(mpr->spin);
    mprWakeWaitService(mpr);
}


int mprSetAppName(MprCtx ctx, cchar *name, cchar *title, cchar *version)
{
    Mpr     *mpr;
    char    *cp;

    mpr = mprGetMpr(ctx);

    if (name) {
        mprFree(mpr->name);
        if ((mpr->name = (char*) mprGetPathBase(mpr, name)) == 0) {
            return MPR_ERR_CANT_ALLOCATE;
        }
        if ((cp = strrchr(mpr->name, '.')) != 0) {
            *cp = '\0';
        }
    }

    if (title) {
        mprFree(mpr->title);
        if ((mpr->title = mprStrdup(ctx, title)) == 0) {
            return MPR_ERR_CANT_ALLOCATE;
        }
    }

    if (version) {
        mprFree(mpr->version);
        if ((mpr->version = mprStrdup(ctx, version)) == 0) {
            return MPR_ERR_CANT_ALLOCATE;
        }
    }
    return 0;
}


cchar *mprGetAppName(MprCtx ctx)
{
    return mprGetMpr(ctx)->name;
}


cchar *mprGetAppTitle(MprCtx ctx)
{
    return mprGetMpr(ctx)->title;
}


/*
 *  Full host name with domain. E.g. "server.domain.com"
 */
void mprSetHostName(MprCtx ctx, cchar *s)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    mprLock(mpr->mutex);
    mprFree(mpr->hostName);
    mpr->hostName = mprStrdup(mpr, s);
    mprUnlock(mpr->mutex);
    return;
}


/*
 *  Return the fully qualified host name
 */
cchar *mprGetHostName(MprCtx ctx)
{
    return mprGetMpr(ctx)->hostName;
}


/*
 *  Server name portion (no domain name)
 */
void mprSetServerName(MprCtx ctx, cchar *s)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    if (mpr->serverName) {
        mprFree(mpr->serverName);
    }
    mpr->serverName = mprStrdup(mpr, s);
    return;
}


/*
 *  Return the server name
 */
cchar *mprGetServerName(MprCtx ctx)
{
    return mprGetMpr(ctx)->serverName;
}


/*
 *  Set the domain name
 */
void mprSetDomainName(MprCtx ctx, cchar *s)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    if (mpr->domainName) {
        mprFree(mpr->domainName);
    }
    mpr->domainName = mprStrdup(mpr, s);
    return;
}


/*
 *  Return the domain name
 */
cchar *mprGetDomainName(MprCtx ctx)
{
    return mprGetMpr(ctx)->domainName;
}


/*
 *  Set the IP address
 */
void mprSetIpAddr(MprCtx ctx, cchar *s)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    if (mpr->ipAddr) {
        mprFree(mpr->ipAddr);
    }
    mpr->ipAddr = mprStrdup(mpr, s);
    return;
}


/*
 *  Return the IP address
 */
cchar *mprGetIpAddr(MprCtx ctx)
{
    return mprGetMpr(ctx)->ipAddr;
}


cchar *mprGetAppVersion(MprCtx ctx)
{
    Mpr *mpr;

    mpr = mprGetMpr(ctx);
    return mpr->version;
}


bool mprGetDebugMode(MprCtx ctx)
{
    return mprGetMpr(ctx)->debugMode;
}


void mprSetDebugMode(MprCtx ctx, bool on)
{
    mprGetMpr(ctx)->debugMode = on;
}


void mprSetLogHandler(MprCtx ctx, MprLogHandler handler, void *handlerData)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);

    mpr->logHandler = handler;
    mpr->logHandlerData = handlerData;
}


MprLogHandler mprGetLogHandler(MprCtx ctx)
{
    return mprGetMpr(ctx)->logHandler;
}


cchar *mprCopyright()
{
    return  "Copyright (c) Embedthis Software LLC, 2003-2011. All Rights Reserved.\n"
            "Copyright (c) Michael O'Brien, 1993-2011. All Rights Reserved.";
}


int mprGetEndian(MprCtx ctx)
{
    char    *probe;
    int     test;

    test = 1;
    probe = (char*) &test;
    return (*probe == 1) ? MPR_LITTLE_ENDIAN : MPR_BIG_ENDIAN;
}


/*
 *  Default memory handler
 */
static void memoryFailure(MprCtx ctx, int64 size, int64 total, bool granted)
{
    if (!granted) {
        mprPrintfError(ctx, "Can't allocate memory block of size %d\n", size);
        mprPrintfError(ctx, "Total memory used %d\n", total);
        exit(255);
    }
    mprPrintfError(ctx, "Memory request for %d bytes exceeds memory red-line\n", size);
    mprPrintfError(ctx, "Total memory used %d\n", total);
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
