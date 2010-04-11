/**
 *  mprSelectWait.c - Wait for I/O by using select.
 *
 *  This module provides I/O wait management for sockets on VxWorks and systems that use select(). Windows and Unix
 *  uses different mechanisms. See mprAsyncSelectWait and mprPollWait. This module is thread-safe.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

#if WINCE || VXWORKS || CYGWIN
/********************************** Forwards **********************************/

static void getWaitFds(MprWaitService *ws);
static void serviceIO(MprWaitService *ws);

/************************************ Code ************************************/

int mprInitSelectWait(MprWaitService *ws)
{
#if BLD_FEATURE_MULTITHREAD
    int     rc, retries, breakPort, breakSock, maxTries;

    maxTries = 100;

    /*
     *  Initialize the "wakeup" socket. This is used to wakeup the service thread if other threads need to wait for I/O.
     */
    mprLock(ws->mutex);

    /*
     *  Try to find a good port to use to break out of the select wait
     */ 
    breakPort = MPR_DEFAULT_BREAK_PORT;
    for (rc = retries = 0; retries < maxTries; retries++) {
        breakSock = socket(AF_INET, SOCK_DGRAM, 0);
        if (breakSock < 0) {
            mprLog(ws, MPR_WARN, "Can't open port %d to use for select. Retrying.\n");
        }
#if BLD_UNIX_LIKE
        fcntl(breakSock, F_SETFD, FD_CLOEXEC);
#endif
        ws->breakAddress.sin_family = AF_INET;
        /*
            Cygwin doesn't work with INADDR_ANY
         */
        // ws->breakAddress.sin_addr.s_addr = INADDR_ANY;
        ws->breakAddress.sin_addr.s_addr = inet_addr("127.0.0.1");;
        ws->breakAddress.sin_port = htons((short) breakPort);
        rc = bind(breakSock, (struct sockaddr *) &ws->breakAddress, sizeof(ws->breakAddress));
        if (breakSock >= 0 && rc == 0) {
#if VXWORKS
            /*
             *  VxWorks 6.0 bug workaround
             */
            ws->breakAddress.sin_port = htons((short) breakPort);
#endif
            break;
        }
        if (breakSock >= 0) {
            closesocket(breakSock);
        }
        breakPort++;
    }

    if (breakSock < 0 || rc < 0) {
        mprLog(ws, MPR_WARN, "Can't bind any port to use for select. Tried %d-%d\n", breakPort, breakPort - maxTries);
        mprUnlock(ws->mutex);
        return MPR_ERR_CANT_OPEN;
    }
    ws->breakSock = breakSock;
    mprUnlock(ws->mutex);
#endif
    return 0;
}


/*
 *  Wait for I/O on a single file descriptor. Return a mask of events found. Mask is the events of interest.
 *  timeout is in milliseconds.
 */
int mprWaitForSingleIO(MprCtx ctx, int fd, int mask, int timeout)
{
    MprWaitService  *ws;
    struct timeval  tval;
    fd_set          readMask, writeMask;

    ws = mprGetMpr(ctx)->waitService;
    tval.tv_sec = timeout / 1000;
    tval.tv_usec = (timeout % 1000) * 1000;

    FD_ZERO(&readMask);
    if (mask & MPR_READABLE) {
        FD_SET(fd, &readMask);
    }
    FD_ZERO(&writeMask);
    if (mask & MPR_WRITABLE) {
        FD_SET(fd, &writeMask);
    }
    if (select(fd + 1, &readMask, &writeMask, NULL, &tval) > 0) {
        mask = 0;
        if (FD_ISSET(fd, &readMask))
            mask |= MPR_READABLE;
        if (FD_ISSET(fd, &writeMask))
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
    struct timeval  tval;
    int             rc;

    /*
     *  No locking. If the masks are updated after this test, the breakout port will wake us up soon.
     */
    if (ws->lastMaskGeneration != ws->maskGeneration) {
        getWaitFds(ws);
    }

    if (ws->flags & MPR_NEED_RECALL) {
        serviceRecall(ws);
        return 1;
    }
#if BLD_DEBUG
    if (mprGetDebugMode(ws) && timeout > 30000) {
        timeout = 30000;
    }
#endif
#if VXWORKS
    /*
     *  To minimize VxWorks task starvation
     */
    timeout = max(timeout, 50);
#endif
    tval.tv_sec = timeout / 1000;
    tval.tv_usec = (timeout % 1000) * 1000;

    /*
        Copy into a stable list while selecting
     */
    ws->selectReadMask = ws->readMask;
    ws->selectWriteMask = ws->writeMask;

    rc = select(ws->maxfd + 1, &ws->selectReadMask, &ws->selectWriteMask, NULL, &tval);
    if (rc > 0) {
        serviceIO(ws);
    }
    return rc;
}


/*
 *  Build the select wait masks
 */
static void getWaitFds(MprWaitService *ws)
{
    MprWaitHandler  *wp, *nextWp;
    int             mask, next;

    mprLock(ws->mutex);

    ws->lastMaskGeneration = ws->maskGeneration;

    FD_ZERO(&ws->readMask);
    FD_ZERO(&ws->writeMask);
    ws->maxfd = 0;

#if BLD_FEATURE_MULTITHREAD
    /*
     *  Add the breakout port to wakeup the service thread when other threads need selecting services.
     */
    FD_SET(ws->breakSock, &ws->readMask);
    ws->maxfd = ws->breakSock + 1;
#endif

    /*
     *  Add an entry for each descriptor desiring service.
     */
    next = 0;
    for (wp = (MprWaitHandler*) mprGetNextItem(ws->handlers, &next); wp; wp = nextWp) {
        nextWp = (MprWaitHandler*) mprGetNextItem(ws->handlers, &next);
        mprAssert(wp->fd >= 0);

        if (wp->proc && wp->desiredMask) {
            /*
             *  The disable mask will be zero when we are already servicing an event. This prevents recursive service.
             */
            mask = wp->desiredMask & wp->disableMask;
            if (mask) {
#if BLD_FEATURE_MULTITHREAD
                if (wp->inUse) continue;
#endif
                if (mask & MPR_READABLE) {
                    FD_SET(wp->fd, &ws->readMask);
                }
                if (mask & MPR_WRITABLE) {
                    FD_SET(wp->fd, &ws->writeMask);
                }
                ws->maxfd = max(ws->maxfd, wp->fd);
            }
        }
    }
    mprUnlock(ws->mutex);
}


/*
 *  Service I/O events
 */
static void serviceIO(MprWaitService *ws)
{
    MprWaitHandler      *wp;
    int                 mask, index;

    /*
     *  Must have the wait list stable while we service events
     */
    mprLock(ws->mutex);

#if BLD_FEATURE_MULTITHREAD
    /*
     *  Service the breakout pipe first
     */
    if (FD_ISSET(ws->breakSock, &ws->selectReadMask)) {
        char        buf[128];
        int         rc;

#if VXWORKS
        int len = sizeof(ws->breakAddress);
        rc = recvfrom(ws->breakSock, buf, sizeof(buf), 0, (struct sockaddr*) &ws->breakAddress, (int*) &len);
#else
        socklen_t   len = sizeof(ws->breakAddress);
        rc = recvfrom(ws->breakSock, buf, sizeof(buf), 0, (struct sockaddr*) &ws->breakAddress, (socklen_t*) &len);
#endif
        if (rc < 0) {
            closesocket(ws->breakSock);
            if (mprInitSelectWait(ws) < 0) {
                mprError(ws, "Can't re-open select breakout port");
            }
        }
        ws->flags &= ~MPR_BREAK_REQUESTED;
    }
#endif

    /*
     *  Now service all IO wait handlers
     */
    for (index = 0; (wp = (MprWaitHandler*) mprGetNextItem(ws->handlers, &index)) != 0; ) {
        mprAssert(wp->fd >= 0);
        /*
         *  Present mask is only cleared after the io handler callback has completed
         */
        mask = 0;
        if ((wp->desiredMask & MPR_READABLE) && FD_ISSET(wp->fd, &ws->selectReadMask)) {
            mask |= MPR_READABLE;
            FD_CLR((uint) wp->fd, &ws->selectReadMask);
        }
        if ((wp->desiredMask & MPR_WRITABLE) && FD_ISSET(wp->fd, &ws->selectWriteMask)) {
            mask |= MPR_WRITABLE;
            FD_CLR((uint) wp->fd, &ws->selectWriteMask);
        }
        if (mask == 0) {
            continue;
        }
        if (mask & wp->desiredMask) {
#if BLD_FEATURE_MULTITHREAD
            /*
             *  Disable events to prevent recursive I/O events. Callback must call mprEnableWaitEvents
             */
            mprAssert(wp->disableMask == -1);
            if (wp->disableMask == 0) {
                /* Should never get here. Just for safety. */
                continue;
            }
            ws->maskGeneration++;
            wp->disableMask = 0;
            mprAssert(wp->inUse == 0);
            wp->inUse++;
#endif
            wp->presentMask = mask;

            mprUnlock(ws->mutex);
            mprInvokeWaitCallback(wp);
            mprLock(ws->mutex);
        }
    }

    mprUnlock(ws->mutex);
}


#if BLD_FEATURE_MULTITHREAD
/*
 *  Wake the wait service (i.e. select/poll call)
 */
void mprWakeOsWaitService(MprCtx ctx)
{
    MprWaitService  *ws;
    int             c, rc;

    ws = mprGetMpr(ctx)->waitService;
    mprLock(ws->mutex);
    if (!(ws->flags & MPR_BREAK_REQUESTED)) {
        ws->flags |= MPR_BREAK_REQUESTED;
        c = 0;
        rc = sendto(ws->breakSock, (char*) &c, 1, 0, (struct sockaddr*) &ws->breakAddress, sizeof(ws->breakAddress));
        if (rc < 0) {
            static int warnOnce = 0;
            if (warnOnce++ == 0) {
                mprLog(ws, 0, "Can't send wakeup to breakout socket: errno %d", errno);
            }
            ws->lastMaskGeneration = 0;
        }
    }
    mprUnlock(ws->mutex);
}
#endif

#else
void __dummyMprSelectWait() {}
#endif /* VXWORKS */

/*
 *  @copy   default
 *
 *  Copyright (c) Embedthis Software LLC, 2003-2010. All Rights Reserved.
 *  Copyright (c) Michael O'Brien, 1993-2010. All Rights Reserved.
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
 *  @end
 */
