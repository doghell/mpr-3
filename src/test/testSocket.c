/**
 *  testSocket.c - Unit tests for mprSocket
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

/*********************************** Locals ***********************************/

typedef struct MprTestSocket {
    MprSocket   *server4;                   /* IPv4 server */
    MprSocket   *server6;                   /* IPv6 server */
    MprBuf      *inBuf;                     /* Input buffer */
    MprSocket   *client;                    /* Client socket */
    int         port;                       /* Server port */
    int         accepted;                   /* Accept */
    int         hasInternet;                /* Has internet connection */
} MprTestSocket;

static int warnNoInternet = 0;
static int bufsize = 16 * 1024;

/***************************** Forward Declarations ***************************/

static int acceptFn(MprSocket *sp, void *gp, cchar *ip, int port);
static int readEvent(MprTestGroup *gp, int mask, bool isWorker);

/************************************ Code ************************************/
/*
 *  Open a server on a free port.
 */
static MprSocket *openServer(MprCtx ctx, cchar *host, MprSocketAcceptProc callback, void *data)
{
    MprSocket       *sp;
    int             port;
    
    sp = mprCreateSocket(ctx, NULL);
    if (sp == 0) {
        return 0;
    }
    
    for (port = 9175; port < 9250; port++) {
        if (mprOpenServerSocket(sp, host, port, acceptFn, (void*) ctx, 0) >= 0) {
            return sp;
        }
    }
    return 0;
}


/*
 *  Initialize the TestSocket structure and find a free server port to listen on.
 *  Also determine if we have an internet connection.
 */
static int initSocket(MprTestGroup *gp)
{
    MprSocket       *sock;
    MprTestSocket   *ts;

    ts = mprAllocObjZeroed(gp, MprTestSocket);
    if (ts == 0) {
        return MPR_ERR_NO_MEMORY;
    }
    gp->data = (void*) ts;

    if (getenv("NO_INTERNET")) {
        warnNoInternet++;
    } else {
        /*
         *  See if we have an internet connection
         */
        sock = mprCreateSocket(gp, NULL);
        if (mprOpenClientSocket(sock, "www.embedthis.com", 80, 0) >= 0) {
            ts->hasInternet = 1;
        }
        mprFree(sock);
    }
    
    /*
     *  Open a server on a free port.
     */
    ts->server4 = openServer(gp, "127.0.0.1", acceptFn, (void*) gp);
    if (ts->server4 == 0) {
        mprError(gp, "testSocket: Can't find free IPv4 server port for testSocket");
        return MPR_ERR_NO_MEMORY;
    }

    ts->server6 = openServer(gp, "::1", acceptFn, (void*) gp);
    if (ts->server6 == 0) {
        mprLog(gp, 2, "testSocket: Can't find free IPv6 server port for testSocket - IPv6 may not be enabled");
    }
    ts->inBuf = mprCreateBuf(ts, 0, 0);
    return 0;
}


static int termSocket(MprTestGroup *gp)
{
    MprTestSocket   *ts;
    
    ts = (MprTestSocket*) gp->data;
    mprFree(ts->server4);
    mprFree(ts->server6);
    mprFree(ts);
    return 0;
}


/*
 *  Test:
 *      - IPv4
 *      - IPv6
 *      - blocking / non-blocking mode
 *      - client & server
 *      - broadcast, datagram, 
 *      - options: reuse, nodelay
 */
static int acceptFn(MprSocket *sp, void *data, cchar *ip, int port)
{
    MprTestSocket   *ts;
    MprTestGroup    *gp;

    gp = (MprTestGroup*) data;
    ts = (MprTestSocket*) gp->data;
    ts->accepted = 1;
    ts->client = sp;
    mprSetSocketCallback(sp, (MprSocketProc) readEvent, (void*) gp, MPR_READABLE, MPR_NORMAL_PRIORITY);
    mprSignalTestComplete(gp);
    return 0;
}


/*
 *  Read incoming data. Expect to read about 5K worth of data.
 */
static int readEvent(MprTestGroup *gp, int mask, bool isWorker)
{
    MprTestSocket   *ts;
    MprSocket       *sp;
    char            *buf;
    int             rc, space, nbytes, len;

    ts = (MprTestSocket*) gp->data;
    sp = ts->client;

    len = mprGetBufLength(ts->inBuf);
    space = mprGetBufSpace(ts->inBuf);
    if (space < (bufsize / 2)) {
        rc = mprGrowBuf(ts->inBuf, bufsize - space);
        assert(rc == 0);
    }

    buf = mprGetBufEnd(ts->inBuf);
    nbytes = mprReadSocket(sp, buf, mprGetBufSpace(ts->inBuf));
    assert(nbytes >= 0);

    if (nbytes < 0) {
        mprCloseSocket(sp, 1);
        mprSignalTestComplete(gp);
        return 1;

    } else if (nbytes == 0) {
        if (mprIsSocketEof(sp)) {
            mprCloseSocket(sp, 1);
            mprSignalTestComplete(gp);
            return 1;
        }

    } else {
        mprAdjustBufEnd(ts->inBuf, nbytes);
    }
    mprEnableSocketEvents(sp);
    return 0;
}


static void testCreateSocket(MprTestGroup *gp)
{
    MprSocket       *sp;

    sp = mprCreateSocket(gp, NULL);
    assert(sp != 0);
    mprFree(sp);
}


static void testClient(MprTestGroup *gp)
{
    MprSocket       *sp;
    MprTestSocket   *ts;
    int             rc;

    ts = (MprTestSocket*) gp->data;
    
    if (ts->hasInternet) {
        sp = mprCreateSocket(gp, NULL);
        assert(sp != 0);
    
        rc = mprOpenClientSocket(sp, "www.google.com", 80, 0);
        assert(rc >= 0);
        mprFree(sp);
        
    } else if (warnNoInternet++ == 0) {
        mprPrintf(gp, "%12s Skipping test %s.testClient: no internet connection.\n", "[Notice]", gp->fullName);
    }
}


static void testClientServer(MprTestGroup *gp, cchar *host, int port)
{
    MprSocket       *client;
    MprTestSocket   *ts;
    char            *buf, *thisBuf;
    int             i, rc, len, thisLen, sofar, nbytes, count;

    ts = (MprTestSocket*) gp->data;
    ts->accepted = 0;

    mprFlushBuf(ts->inBuf);
    
    client = mprCreateSocket(gp, NULL);
    assert(client != 0);

    /*
     *  Open client connection
     */
    rc = mprOpenClientSocket(client, host, port, 0);
    assert(rc >= 0);

    /*  Set in acceptFn() */
    mprWaitForTestToComplete(gp, MPR_TEST_SLEEP);
    assert(ts->accepted);

    buf = "01234567890123456789012345678901234567890123456789\r\n";
    len = strlen(buf);

    /*
     *  Write a set of lines to the client. Server should receive. Use non-blocking mode. This writes about 5K of data.
     */
    mprSetSocketBlockingMode(client, 0);
    sofar = 0;
    count = 100;
    for (i = 0; i < count; i++) {
        /*
         *  Non-blocking I/O may return a short-write
         */
        thisBuf = buf;
        for (thisLen = len; thisLen > 0; ) {
            nbytes = mprWriteSocket(client, thisBuf, thisLen);
            assert(nbytes >= 0);
            thisLen -= nbytes;
            thisBuf += nbytes;
#if !BLD_FEATURE_MULTITHREAD
            if (nbytes == 0) {
                mprServiceEvents(mprGetDispatcher(client), 10, MPR_SERVICE_EVENTS | MPR_SERVICE_IO | MPR_SERVICE_ONE_THING);
            }
#endif
        }
    }
    mprAssert(mprGetBufLength(ts->inBuf) < 16 * 1024);
    mprCloseSocket(client, 1);

    mprWaitForTestToComplete(gp, MPR_TEST_SLEEP);
    assert(mprGetBufLength(ts->inBuf) == (count * len));
    mprFlushBuf(ts->inBuf);

    mprFree(client); 
}


static void testClientServerIPv4(MprTestGroup *gp)
{
    MprTestSocket   *ts;
    
    ts = (MprTestSocket*) gp->data;
    
    testClientServer(gp, ts->server4->ipAddr, ts->server4->port);
}


static void testClientServerIPv6(MprTestGroup *gp)
{
    MprTestSocket   *ts;
    
    ts = (MprTestSocket*) gp->data;
    
    if (ts->server6) {
        testClientServer(gp, ts->server6->ipAddr, ts->server6->port);
    }
}


static void testClientSslv4(MprTestGroup *gp)
{
    MprSocket       *sp;
    MprTestSocket   *ts;
    int             rc;

    ts = (MprTestSocket*) gp->data;
    
    if (ts->hasInternet) {
        if (mprHasSecureSockets(gp)) {
            sp = mprCreateSocket(gp, NULL);
            assert(sp != 0);
            assert(sp->provider != 0);
        
            rc = mprOpenClientSocket(sp, "www.google.com", 443, 0);
            assert(rc >= 0);
            mprFree(sp);
        }
        
    } else if (warnNoInternet++ == 0) {
        mprPrintf(gp, "%12s Skipping test %s.testClientSslv4: no internet connection.\n", "[Notice]", gp->fullName);
    }
}


MprTestDef testSocket = {
    "socket", 0, initSocket, termSocket,
    {
        MPR_TEST(0, testCreateSocket),
        MPR_TEST(0, testClient),
        MPR_TEST(0, testClientServerIPv4),
        MPR_TEST(0, testClientServerIPv6),
        MPR_TEST(0, testClientSslv4),
        MPR_TEST(0, 0),
    },
};


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
