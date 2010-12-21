/**
 *  mprAsyncSelectWait.c - Wait for I/O on Windows.
 *
 *  This module provides io management for sockets on Windows like systems. 
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

#if BLD_WIN_LIKE && !WINCE

/***************************** Forward Declarations ***************************/

static LRESULT msgProc(HWND hwnd, uint msg, uint wp, long lp);

/************************************ Code ************************************/

int mprInitSelectWait(MprWaitService *ws)
{   
    mprGetMpr(ws)->waitService = ws;
    mprInitWindow(mprGetMpr(ws)->waitService);
    return 0;
}


/*
 *  Wait for I/O on a single descriptor. Return the number of I/O events found. Mask is the events of interest.
 *  Timeout is in milliseconds.
 */
int mprWaitForSingleIO(MprCtx ctx, int fd, int desiredMask, int timeout)
{
    HANDLE      h;
    int         winMask;

    winMask = 0;
    if (desiredMask & MPR_READABLE) {
        winMask |= /* FD_CONNECT | */ FD_CLOSE | FD_READ;
    }
    if (desiredMask & MPR_WRITABLE) {
        winMask |= FD_WRITE;
    }
    h = CreateEvent(NULL, FALSE, FALSE, "mprWaitForSingleIO");
    WSAEventSelect(fd, h, winMask);
    if (WaitForSingleObject(h, timeout) == WAIT_OBJECT_0) {
        CloseHandle(h);
        return MPR_READABLE | MPR_WRITABLE;
    }
    CloseHandle(h);
    return 0;
}


/*
 *  Wait for I/O on all registered descriptors. Timeout is in milliseconds. Return the number of events serviced.
 */
int mprWaitForIO(MprWaitService *ws, int timeout)
{
    MSG             msg;
    int             count, rc;

    mprAssert(ws->hwnd);

#if BLD_DEBUG
    if (mprGetDebugMode(ws) && timeout > 30000) {
        timeout = 30000;
    }
#endif
    rc = SetTimer(ws->hwnd, 0, timeout, NULL);
    mprAssert(rc != 0);

    count = 0;
    if (GetMessage(&msg, NULL, 0, 0) != 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
        count++;
    } else {
        mprTerminate(ws, 1);
    }
    return count;
}


void mprServiceWinIO(MprWaitService *ws, int sockFd, int winMask)
{
    MprWaitHandler      *wp;
    int                 index, mask;

    mprLock(ws->mutex);
    ws->flags &= ~MPR_BREAK_REQUESTED;

    for (index = 0; (wp = (MprWaitHandler*) mprGetNextItem(ws->handlers, &index)) != 0; ) {
        if (wp->fd == sockFd) {
            break;
        }
    }
    if (wp == 0) {
        /*
         *  If the server forcibly closed the socket, we may still get a read event. Just ignore it.
         */
        mprUnlock(ws->mutex);
        return;
    }

    /*
     *  disableMask will be zero if we are already servicing an event
     */
    mask = wp->desiredMask & wp->disableMask;
    if (mask == 0 || wp->inUse > 0) {
        /*
         *  Already have an event scheduled so we must not schedule another yet. We should have disabled events, 
         *  but a message may already be in the message queue.
         */
        mprUnlock(ws->mutex);
        return;
    }

    /*
     *  Mask values: READ==1, WRITE=2, ACCEPT=8, CONNECT=10, CLOSE=20
     */
    wp->presentMask = 0;
    if (winMask & (FD_READ | FD_ACCEPT | FD_CLOSE)) {
        wp->presentMask |= MPR_READABLE;
    }
    if (winMask & (FD_WRITE | FD_CONNECT)) {
        wp->presentMask |= MPR_WRITABLE;
    }

    if (wp->presentMask) {
#if BLD_FEATURE_MULTITHREAD
        wp->disableMask = 0;
        ws->maskGeneration++;
        wp->inUse++;
#endif
        mprUnlock(ws->mutex);
        mprInvokeWaitCallback(wp);
    } else {
        mprUnlock(ws->mutex);
    }
}


#if BLD_FEATURE_MULTITHREAD
/*
 *  Wake the wait service
 */
void mprWakeOsWaitService(MprCtx ctx)
{
    MprWaitService  *ws;
   
    ws = mprGetMpr(ctx)->waitService;
    mprLock(ws->mutex);
    if (!(ws->flags & MPR_BREAK_REQUESTED)) {
        ws->flags |= MPR_BREAK_REQUESTED;
        if (ws->hwnd) {
            PostMessage(ws->hwnd, WM_NULL, 0, 0L);
        }
    }
    mprUnlock(ws->mutex);
}
#endif


/*
 *  Apply wait handler updates that occurred while the wait handler was in use
 */
void mprUpdateWaitHandler(MprWaitHandler *wp, bool wakeup)
{
    MprWaitService  *ws;
    int             eligible, winMask;

    if (!wp->inUse && wp->flags & (MPR_WAIT_RECALL_HANDLER | MPR_WAIT_MASK_CHANGED)) {
        ws = wp->waitService;
        if (wp->flags & MPR_WAIT_RECALL_HANDLER) {
            PostMessage(ws->hwnd, ws->socketMessage, wp->fd, FD_READ);
            wp->flags &= ~MPR_WAIT_RECALL_HANDLER;
            return;
        }
        winMask = 0;
        eligible = wp->desiredMask & wp->disableMask;
        if (eligible & MPR_READABLE) {
            winMask |= FD_ACCEPT | FD_CONNECT | FD_CLOSE | FD_READ;
        }
        if (eligible & MPR_WRITABLE) {
            winMask |= FD_WRITE;
        }
        WSAAsyncSelect(wp->fd, ws->hwnd, ws->socketMessage, winMask);
    }
}


/*
 *  Create a default window if the application has not already created one.
 */ 
int mprInitWindow(MprWaitService *ws)
{
    Mpr         *mpr;
    WNDCLASS    wc;
    HWND        hwnd;
    int         rc;

    mpr = mprGetMpr(ws);

    if (ws->hwnd) {
        return 0;
    }

    wc.style            = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground    = (HBRUSH) (COLOR_WINDOW+1);
    wc.hCursor          = LoadCursor(NULL, IDC_ARROW);
    wc.cbClsExtra       = 0;
    wc.cbWndExtra       = 0;
    wc.hInstance        = 0;
    wc.hIcon            = NULL;
    wc.lpfnWndProc      = (WNDPROC) msgProc;

    wc.lpszMenuName     = wc.lpszClassName = mprGetAppName(mpr);

    rc = RegisterClass(&wc);
    if (rc == 0) {
        mprError(mpr, "Can't register windows class");
        return MPR_ERR_CANT_INITIALIZE;
    }

    hwnd = CreateWindow(mprGetAppName(mpr), mprGetAppTitle(mpr), WS_OVERLAPPED, CW_USEDEFAULT, 0, 0, 0, NULL, NULL, 0, NULL);
    if (!hwnd) {
        mprError(mpr, "Can't create window");
        return -1;
    }
    ws->hwnd = hwnd;
    ws->socketMessage = MPR_SOCKET_MESSAGE;
    return 0;
}


/*
 *  Windows message processing loop for wakeup and socket messages
 */
static LRESULT msgProc(HWND hwnd, uint msg, uint wp, long lp)
{
    Mpr                 *mpr;
    MprWaitService      *ws;
    int                 sock, winMask;

    mpr = mprGetMpr(0);
    ws = mpr->waitService;

    if (msg == WM_DESTROY || msg == WM_QUIT) {
        mprTerminate(mpr, 1);

    } else if (msg && msg == ws->socketMessage) {
        sock = wp;
        winMask = LOWORD(lp);
        mprServiceWinIO(mpr->waitService, sock, winMask);

    } else if (ws->msgCallback) {
        ws->msgCallback(hwnd, msg, wp, lp);

    } else {
        return DefWindowProc(hwnd, msg, wp, lp);
    }
    return 0;
}


void mprSetWinMsgCallback(MprWaitService *ws, MprMsgCallback callback)
{
    ws->msgCallback = callback;
}


#else
void __mprAsyncDummy() {}
#endif /* BLD_WIN_LIKE */

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
