/**
 *  mprPollWait.c - Wait for I/O by using poll on unix like systems.
 *
 *  This module augments the mprWait wait services module by providing poll() based waiting support.
 *  Also see mprAsyncSelectWait and mprSelectWait. This module is thread-safe.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

#if LINUX || MACOSX || FREEBSD
/********************************** Forwards **********************************/

static void getWaitFds(MprWaitService *ws);
static void growFds(MprWaitService *ws);
static void serviceIO(MprWaitService *ws, struct pollfd *stableFds, int count);

/************************************ Code ************************************/

int mprInitSelectWait(MprWaitService *ws)
{
#if BLD_FEATURE_MULTITHREAD
    /*
     *  Initialize the "wakeup" pipe. This is used to wakeup the service thread if other threads need to wait for I/O.
     */
    if (pipe(ws->breakPipe) < 0) {
        mprError(ws, "Can't open breakout pipe");
        return MPR_ERR_CANT_INITIALIZE;
    }
    fcntl(ws->breakPipe[0], F_SETFL, fcntl(ws->breakPipe[0], F_GETFL) | O_NONBLOCK);
    fcntl(ws->breakPipe[1], F_SETFL, fcntl(ws->breakPipe[1], F_GETFL) | O_NONBLOCK);
#endif
    return 0;
}


/*
 *  Wait for I/O on a single file descriptor. Return a mask of events found. Mask is the events of interest.
 *  timeout is in milliseconds.
 */
int mprWaitForSingleIO(MprCtx ctx, int fd, int mask, int timeout)
{
    struct pollfd   fds[1];

    fds[0].fd = fd;
    fds[0].events = 0;
    fds[0].revents = 0;

    if (mask & MPR_READABLE)
        fds[0].events |= POLLIN;
    if (mask & MPR_WRITABLE)
        fds[0].events |= POLLOUT;
    if (poll(fds, 1, timeout) > 0) {
        mask = 0;
        if (fds[0].revents & POLLIN)
            mask |= MPR_READABLE;
        if (fds[0].revents & POLLOUT)
            mask |= MPR_WRITABLE;
        return mask;
    }
    return 0;
}


static void serviceRecall(MprWaitService *ws)
{
    MprWaitHandler      *wp;
    int                 index;

    mprLock(ws->mutex);
    ws->flags &= ~MPR_NEED_RECALL;
    for (index = 0; (wp = (MprWaitHandler*) mprGetNextItem(ws->handlers, &index)) != 0; ) {
        if (wp->flags & MPR_WAIT_RECALL_HANDLER) {
            if ((wp->desiredMask & wp->disableMask) && wp->inUse == 0) {
                wp->presentMask |= MPR_READABLE;
                wp->flags &= ~MPR_WAIT_RECALL_HANDLER;
#if BLD_FEATURE_MULTITHREAD
                mprAssert(wp->disableMask == -1);
                ws->maskGeneration++;
                wp->disableMask = 0;
                mprAssert(wp->inUse == 0);
                wp->inUse++;
#endif
                mprUnlock(ws->mutex);
                mprInvokeWaitCallback(wp);
                mprLock(ws->mutex);

            } else {
                ws->flags |= MPR_NEED_RECALL;
            }
        }
    }
    mprUnlock(ws->mutex);
}


/*
 *  Wait for I/O on all registered file descriptors. Timeout is in milliseconds. Return the number of events detected.
 */
int mprWaitForIO(MprWaitService *ws, int timeout)
{
    struct pollfd   *fds;
    int             rc, count;

    /*
     *  No locking. If the masks are updated after this test, the breakout pipe will wake us up soon.
     */
    mprLock(ws->mutex);
    if (ws->lastMaskGeneration != ws->maskGeneration) {
        getWaitFds(ws);
    }
    if (ws->flags & MPR_NEED_RECALL) {
        mprUnlock(ws->mutex);
        serviceRecall(ws);
        return 1;
    } else
#if BLD_DEBUG
    if (mprGetDebugMode(ws) && timeout > 30000) {
        timeout = 30000;
    }
#endif
    count = ws->fdsCount;
    if ((fds = mprMemdup(ws, ws->fds, count * sizeof(struct pollfd))) == 0) {
        mprUnlock(ws->mutex);
        return MPR_ERR_NO_MEMORY;
    }
    mprUnlock(ws->mutex);

    rc = poll(fds, count, timeout);
    if (rc < 0) {
        mprLog(ws, 2, "Poll returned %d, errno %d", rc, mprGetOsError());
    } else if (rc > 0) {
        serviceIO(ws, fds, count);
    }
    mprFree(fds);
    return rc;
}


/*
 *  Get the waiting file descriptors
 */
static void getWaitFds(MprWaitService *ws)
{
    MprWaitHandler  *wp, *nextWp;
    struct pollfd   *pollfd;
    int             mask, next;

    mprLock(ws->mutex);

    ws->lastMaskGeneration = ws->maskGeneration;
    growFds(ws);
    pollfd = ws->fds;

#if BLD_FEATURE_MULTITHREAD
    /*
     *  Add the breakout port to wakeup the service thread when other threads need selecting services.
     */
    pollfd->fd = ws->breakPipe[MPR_READ_PIPE];
    pollfd->events = POLLIN;
    pollfd++;
#endif

    /*
     *  Add an entry for each descriptor desiring service.
     */
    next = 0;
    for (wp = (MprWaitHandler*) mprGetNextItem(ws->handlers, &next); wp; wp = nextWp) {
        nextWp = (MprWaitHandler*) mprGetNextItem(ws->handlers, &next);

        if (wp->fd >= 0 && wp->proc && wp->desiredMask) {
            /*
             *  The disable mask will be zero when we are already servicing an event. This prevents recursive service.
             */
            mask = wp->desiredMask & wp->disableMask;
            if (mask) {
#if BLD_FEATURE_MULTITHREAD
                if (wp->inUse) continue;
#endif
                pollfd->events = 0;
                if (mask & MPR_READABLE) {
                    pollfd->events |= POLLIN;
                }
                if (mask & MPR_WRITABLE) {
                    pollfd->events |= POLLOUT;
                }
                if (pollfd->events) {
                    pollfd->fd = wp->fd;
                    pollfd++;
                }
            }
        }
    }
    ws->fdsCount = (int) (pollfd - ws->fds);
    mprAssert(ws->fdsCount <= ws->fdsSize);
    mprUnlock(ws->mutex);
}


/*
 *  Service I/O events
 */
static void serviceIO(MprWaitService *ws, struct pollfd *fds, int count)
{
    MprWaitHandler      *wp;
    struct pollfd       *fp;
    int                 i, mask, index, start;

    /*
     *  Must have the wait list stable while we service events
     */
    mprLock(ws->mutex);
    start = 0;
    
#if BLD_FEATURE_MULTITHREAD
    mprAssert(mprGetCurrentOsThread(ws) == mprGetMpr(ws)->serviceThread);

    /*
     *  Service the breakout pipe first
     */
    if (fds[0].revents & POLLIN) {
        char    buf[128];
        if (read(ws->breakPipe[MPR_READ_PIPE], buf, sizeof(buf)) < 0) {
            /* Ignore */
        }
        ws->flags &= ~MPR_BREAK_REQUESTED;
    }
    start++;
#endif

    /*
     *  Now service all IO wait handlers. Processing must be aborted if an active fd is removed.
     */
    for (i = start; i < count; ) {
        fp = &fds[i++];
        if (fp->revents == 0) {
            continue;
        }

        /*
         *  Go in reverse order to maximize the chance of getting the most active connection
         */
        for (index = -1; (wp = (MprWaitHandler*) mprGetPrevItem(ws->handlers, &index)) != 0; ) {
            mprAssert(wp->fd >= 0);
            if (wp->fd != fp->fd) {
                continue;
            }
            /*
             *  Present mask is only cleared after the io handler callback has completed
             */
            mask = 0;
            if ((wp->desiredMask & MPR_READABLE) && fp->revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL)) {
                mask |= MPR_READABLE;
                fp->revents &= ~(POLLIN | POLLHUP | POLLERR | POLLNVAL);
            }
            if ((wp->desiredMask & MPR_WRITABLE) && fp->revents & POLLOUT) {
                mask |= MPR_WRITABLE;
                fp->revents &= ~POLLOUT;
            }
            if (wp->flags & MPR_WAIT_RECALL_HANDLER) {
                if (wp->desiredMask & wp->disableMask) {
                    mask |= MPR_READABLE;
                    wp->flags &= ~MPR_WAIT_RECALL_HANDLER;
                } else {
                    mprAssert(wp->desiredMask & wp->disableMask);
                }
            }
            if (mask == 0) {
                break;
            }
            if (mask & wp->desiredMask) {
                wp->presentMask = mask;
#if BLD_FEATURE_MULTITHREAD
                /*
                 *  Disable events to prevent recursive I/O events. Callback must call mprEnableWaitEvents
                 */
                mprAssert(wp->disableMask == -1);
                if (wp->disableMask == 0) {
                    /* Should not ever get here. Just for safety. */
                    break;
                }
                ws->maskGeneration++;
                wp->disableMask = 0;
                mprAssert(wp->inUse == 0);
                wp->inUse++;
#endif
                mprUnlock(ws->mutex);
                mprInvokeWaitCallback(wp);
                mprLock(ws->mutex);
            }
            break;
        }
        fp->revents = 0;
    }
    mprUnlock(ws->mutex);
}


#if BLD_FEATURE_MULTITHREAD
void mprWakeOsWaitService(MprCtx ctx)
{
    MprWaitService  *ws;
    int             c;

    ws = mprGetMpr(ctx)->waitService;
    mprLock(ws->mutex);
    if (!(ws->flags & MPR_BREAK_REQUESTED)) {
        ws->flags |= MPR_BREAK_REQUESTED;
        c = 0;
        if (write(ws->breakPipe[MPR_WRITE_PIPE], (char*) &c, 1) < 0) {
            mprError(ctx, "Can't write to break pipe");
        }
    }
    mprUnlock(ws->mutex);
}
#endif


/*
 *  Grow the fds list as required. Never shrink.
 */
static void growFds(MprWaitService *ws)
{
    int     len;

    len = max(ws->fdsSize, mprGetListCount(ws->handlers) + 1);
    if (len > ws->fdsSize) {
        ws->fds = mprRealloc(ws, ws->fds, len * sizeof(struct pollfd));
        if (ws->fds == 0) {
            /*  Global memory allocation handler will handle this */
            return;
        }
        memset(&ws->fds[ws->fdsSize], 0, (len - ws->fdsSize) * sizeof(struct pollfd));
        ws->fdsSize = len;
    }
}


#else
void __mprDummyPollWait() {}
#endif /* BLD_UNIX_LIKE */

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
    vim: sw=8 ts=8 expandtab

    @end
 */
