/**
 *  mprWait.c - Wait for I/O service.
 *
 *  This module provides wait management for sockets and other file descriptors and allows users to create wait
 *  handlers which will be called when I/O events are detected. Multiple backends (one at a time) are supported.
 *
 *  This module is thread-safe.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

/***************************** Forward Declarations ***************************/

static int  handlerDestructor(MprWaitHandler *wp);

/************************************ Code ************************************/
/*
 *  Initialize the service
 */
MprWaitService *mprCreateWaitService(Mpr *mpr)
{
    MprWaitService  *ws;

    ws = mprAllocObjZeroed(mpr, MprWaitService);
    if (ws == 0) {
        return 0;
    }
    ws->flags = 0;
    ws->maskGeneration = 0;
    ws->lastMaskGeneration = -1;
    ws->handlers = mprCreateList(ws);

#if BLD_WIN_LIKE && !WINCE
    ws->socketMessage = MPR_SOCKET_MESSAGE;
#endif
#if BLD_FEATURE_MULTITHREAD
    ws->mutex = mprCreateLock(ws);
#endif
    mprInitSelectWait(ws);
    return ws;
}


/*
 *  Create a handler. Priority is only observed when multi-threaded.
 */
MprWaitHandler *mprCreateWaitHandler(MprCtx ctx, int fd, int mask, MprWaitProc proc, void *data, int pri, int flags)
{
    MprWaitService  *ws;
    MprWaitHandler  *wp;

    mprAssert(fd >= 0);

    ws = mprGetMpr(ctx)->waitService;

    if (mprGetListCount(ws->handlers) == FD_SETSIZE) {
        mprError(ws, "io: Too many io handlers: %d\n", FD_SETSIZE);
        return 0;
    }

    wp = mprAllocObjWithDestructorZeroed(ws, MprWaitHandler, handlerDestructor);
    if (wp == 0) {
        return 0;
    }
#if BLD_UNIX_LIKE || VXWORKS
    if (fd >= FD_SETSIZE) {
        mprError(ws, "File descriptor %d exceeds max io of %d", fd, FD_SETSIZE);
    }
#endif
    if (pri == 0) {
        pri = MPR_NORMAL_PRIORITY;
    }

    wp->fd              = fd;
    wp->proc            = proc;
    wp->flags           = flags | MPR_WAIT_MASK_CHANGED;
    wp->handlerData     = data;
    wp->disableMask     = -1;
    wp->waitService     = ws;
    wp->desiredMask     = mask;
#if BLD_FEATURE_MULTITHREAD
    wp->priority        = pri;
#endif

    mprLock(ws->mutex);
    if (mprAddItem(ws->handlers, wp) < 0) {
        mprUnlock(ws->mutex);
        mprFree(wp);
        return 0;
    }
    mprUnlock(ws->mutex);
    mprUpdateWaitHandler(wp, 1);
    return wp;
}


/*
 *  Wait handler Destructor. Called from mprFree.
 */
static int handlerDestructor(MprWaitHandler *wp)
{
    mprDisconnectWaitHandler(wp);
    return 0;
}


/*
 *  Disconnect a wait handler so it cannot be invoked. The memory is still intact.
 */
void mprDisconnectWaitHandler(MprWaitHandler *wp)
{
    MprWaitService      *ws;

    ws = wp->waitService;

    /*
     *  Lock the service to stabilize the list, then lock the handler to prevent callbacks. 
     */
    mprLock(ws->mutex);
    mprRemoveItem(ws->handlers, wp);

#if BLD_FEATURE_MULTITHREAD
    /*
     *  Extra measures if multi-threaded to catch worker threads that have already been dispatched.
     *  If there is an active callback on another thread, wait for it to complete.
     */
    if (wp->inUse == 0 || wp->thread == mprGetCurrentThread(ws)) {
        /*
         *  Either the callback is not active or this thread is the callback. Either case, removal is okay.
         */
        mprUnlock(ws->mutex);

    } else { 
        MprTime     mark;

        /* wp->inUse could be cleared any time - even while locked as the callback runs unlocked */
        wp->callbackComplete = mprCreateCond(wp);
        wp->flags |= MPR_WAIT_DESTROYING;
        mprUnlock(ws->mutex);

        mark = mprGetTime(ws);
        while (wp->inUse > 0) {
            if (mprWaitForCond(wp->callbackComplete, 10) == 0) {
                break;
            }
            if (mprGetElapsedTime(ws, mark) > MPR_TIMEOUT_HANDLER) {
                break;
            }
        }
    }
#endif
    ws->maskGeneration++;
    mprWakeWaitService(ws);
}


#if BLD_FEATURE_MULTITHREAD
/*
 *  Designate the required worker thread to run the callback
 */
void mprDedicateWorkerToHandler(MprWaitHandler *wp, MprWorker *worker)
{
    wp->requiredWorker = worker;
    mprDedicateWorker(worker);
}


void mprReleaseWorkerFromHandler(MprWaitHandler *wp, MprWorker *worker)
{
    wp->requiredWorker = 0;
    mprReleaseWorker(worker);
}


/*
 *  Cleanup after the callback has run. This is called once the worker is back on the idle queue, with the service locked.
 */
static void waitCleanup(MprWaitHandler *wp, MprWorker *worker)
{
    wp->inUse = 0;
    if (wp->flags & MPR_WAIT_DESTROYING) {
        mprSignalCond(wp->callbackComplete);
    } else {
        mprUpdateWaitHandler(wp, 1);
    }
}


/*
 *  Called by the mprInvokeWaitCallback either directory or indirectly via a worker thread.
 *  WARNING: Called unlocked with inUse set.
 */
static void waitCallback(MprWaitHandler *wp, MprWorker *worker)
{
    MprWaitService      *ws;

    mprAssert(wp->disableMask == 0);
    mprAssert(wp->inUse == 1);

    ws = wp->waitService;
    if (wp->flags & MPR_WAIT_DESTROYING) {
        wp->inUse = 0;
        return;
    }
    wp->thread = mprGetCurrentThread(wp);

    /* 
     *  Configure a cleanup for the callback if it has not been deleted (returns non-zero) and if there is work to do.
     */
    if ((wp->proc)(wp->handlerData, wp->presentMask) == 0) {
        if (wp->flags & (MPR_WAIT_RECALL_HANDLER | MPR_WAIT_MASK_CHANGED | MPR_WAIT_DESTROYING)) {
            if (worker == 0) {
                waitCleanup(wp, NULL);
            } else {
                worker->cleanup = (MprWorkerProc) waitCleanup;
            }
        } else {
            wp->inUse = 0;
        }
    }
}


void mprWakeWaitService(MprCtx ctx)
{
    if (mprMustWakeDispatcher(ctx)) {
        mprWakeOsWaitService(ctx);
    }
}
#endif


/*
 *  Invoke the wait handler callback. Invoked by the wait backend and indirectly via a worker thread. 
 */
void mprInvokeWaitCallback(MprWaitHandler *wp)
{
    MprWaitService      *ws;

    /* Entry with the the service locked */

    ws = wp->waitService;
    if (wp->flags & MPR_WAIT_DESTROYING) {
        return;
    }
#if BLD_FEATURE_MULTITHREAD
    mprAssert(wp->inUse > 0);

    if (wp->requiredWorker) {
        mprActivateWorker(wp->requiredWorker, (MprWorkerProc) waitCallback, (void*) wp, MPR_REQUEST_PRIORITY);
        return;
    } else {
        if (mprStartWorker(wp, (MprWorkerProc) waitCallback, (void*) wp, MPR_REQUEST_PRIORITY) == 0) {
            return;
        }
    }
    /* Can't create a new worker, so fall through and use the service events thread */
    waitCallback(wp, NULL);
#else
    /*
     *  Single-threaded - invoke the callback directly
     */
    (wp->proc)(wp->handlerData, wp->presentMask);
#endif
}


void mprSetWaitCallback(MprWaitHandler *wp, MprWaitProc newProc, int mask)
{
    mprLock(wp->waitService->mutex);
    wp->proc = newProc;
    mprSetWaitEvents(wp, mask, wp->disableMask);
    mprUnlock(wp->waitService->mutex);
}


void mprSetWaitEvents(MprWaitHandler *wp, int desiredMask, int disableMask)
{
    MprWaitService  *ws;

    ws = wp->waitService;
    mprLock(ws->mutex);
    if (wp->desiredMask != desiredMask || wp->disableMask != disableMask) {
        wp->desiredMask = desiredMask;
        wp->disableMask = disableMask;
        wp->flags |= MPR_WAIT_MASK_CHANGED;
        mprUpdateWaitHandler(wp, 1);
    }
    mprUnlock(ws->mutex);
}


void mprDisableWaitEvents(MprWaitHandler *wp)
{
    if (wp->disableMask != 0) {
        mprLock(wp->waitService->mutex);
        wp->disableMask = 0;
        wp->flags |= MPR_WAIT_MASK_CHANGED;
        mprUpdateWaitHandler(wp, 1);
        mprUnlock(wp->waitService->mutex);
    }
}


void mprEnableWaitEvents(MprWaitHandler *wp)
{
    if (wp->disableMask != -1) {
        mprLock(wp->waitService->mutex);
        wp->disableMask = -1;
        wp->flags |= MPR_WAIT_MASK_CHANGED;
        mprUpdateWaitHandler(wp, 1);
        mprUnlock(wp->waitService->mutex);
    }
}


/*
 *  Set a handler to be recalled without further I/O. May be called with a null wp.
 */
void mprRecallWaitHandler(MprWaitHandler *wp)
{
    if (wp) {
        wp->flags |= MPR_WAIT_RECALL_HANDLER;
        mprUpdateWaitHandler(wp, 1);
    }
}


#if BLD_UNIX_LIKE || VXWORKS || WINCE
void mprUpdateWaitHandler(MprWaitHandler *wp, bool wakeup)
{
    MprWaitService  *ws;

    /*
     *  If the handler callback is in-use, don't bother to awaken the wait service yet. 
     *  This routine will be recalled when inUse is zero on callback exit.
     */
    if (!wp->inUse && wp->flags & (MPR_WAIT_RECALL_HANDLER | MPR_WAIT_MASK_CHANGED)) {
        ws = wp->waitService;
        if (wp->flags & MPR_WAIT_RECALL_HANDLER) {
            ws->flags |= MPR_NEED_RECALL;
        }
        if (wp->flags & MPR_WAIT_MASK_CHANGED) {
            wp->flags &= ~MPR_WAIT_MASK_CHANGED;
            ws->maskGeneration++;
        }
        if (wakeup) {
            mprWakeWaitService(wp->waitService);
        }
    }
}
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
