/**
 *  mprSocket.c - Convenience class for the management of sockets
 *
 *  This module provides a higher level interface to interact with the standard sockets API. It does not perform
 *  buffering.
 *
 *  This module is thread-safe.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

#if !VXWORKS && !WINCE
/*
 *  Set this to 0 to disable the IPv6 address service. You can do this if you only need IPv4.
 *  On MAC OS X, getaddrinfo is not thread-safe and crashes when called by a 2nd thread at any time. ie. locking wont help.
 */
#define BLD_HAS_GETADDRINFO 1
#endif

/******************************* Forward Declarations *************************/

static int acceptProc(MprSocket *sp, int mask);
static MprSocket *acceptSocket(MprSocket *sp, bool invokeCallback);
static void closeSocket(MprSocket *sp, bool gracefully);
static int  connectSocket(MprSocket *sp, cchar *host, int port, int initialFlags);
static MprSocket *createSocket(MprCtx ctx, struct MprSsl *ssl);
static MprSocketProvider *createStandardProvider(MprSocketService *ss);
static void disconnectSocket(MprSocket *sp);
static int  flushSocket(MprSocket *sp);
static int  getSocketInfo(MprCtx ctx, cchar *host, int port, int *family, struct sockaddr **addr, socklen_t *addrlen);
static int  getSocketIpAddr(MprCtx ctx, struct sockaddr *addr, int addrlen, char *ipAddr, int size, int *port);
static int  ioProc(MprSocket *sp, int mask);
static int  listenSocket(MprSocket *sp, cchar *host, int port, MprSocketAcceptProc acceptFn, void *data, int initialFlags);
static int  readSocket(MprSocket *sp, void *buf, int bufsize);
static int  socketDestructor(MprSocket *sp);
static int  writeSocket(MprSocket *sp, void *buf, int bufsize);

/************************************ Code ************************************/
/*
 *  Open the socket service
 */

MprSocketService *mprCreateSocketService(MprCtx ctx)
{
    MprSocketService    *ss;

    mprAssert(ctx);

    ss = mprAllocObjZeroed(ctx, MprSocketService);
    if (ss == 0) {
        return 0;
    }
    ss->next = 0;
    ss->maxClients = INT_MAX;
    ss->numClients = 0;

    ss->standardProvider = createStandardProvider(ss);
    if (ss->standardProvider == NULL) {
        mprFree(ss);
        return 0;
    }
    ss->secureProvider = NULL;

#if BLD_FEATURE_MULTITHREAD
    ss->mutex = mprCreateLock(ss);
    if (ss->mutex == 0) {
        mprFree(ss);
        return 0;
    }
#endif
    return ss;
}


/*
 *  Start the socket service
 */
int mprStartSocketService(MprSocketService *ss)
{
    char            hostName[MPR_MAX_IP_NAME], serverName[MPR_MAX_IP_NAME], domainName[MPR_MAX_IP_NAME], *dp;

    mprAssert(ss);

    serverName[0] = '\0';
    domainName[0] = '\0';
    hostName[0] = '\0';

    if (gethostname(serverName, sizeof(serverName)) < 0) {
        mprStrcpy(serverName, sizeof(serverName), "localhost");
        mprUserError(ss, "Can't get host name. Using \"localhost\".");
        /* Keep going */
    }

    if ((dp = strchr(serverName, '.')) != 0) {
        mprStrcpy(hostName, sizeof(hostName), serverName);
        *dp++ = '\0';
        mprStrcpy(domainName, sizeof(domainName), dp);

    } else {
        mprStrcpy(hostName, sizeof(hostName), serverName);
    }
    mprSetServerName(ss, serverName);
    mprSetDomainName(ss, domainName);
    mprSetHostName(ss, hostName);
    return 0;
}


void mprStopSocketService(MprSocketService *ss)
{
}


static MprSocketProvider *createStandardProvider(MprSocketService *ss)
{
    MprSocketProvider   *provider;

    provider = mprAllocObj(ss, MprSocketProvider);
    if (provider == 0) {
        return 0;
    }
    provider->name = "standard";
    provider->acceptSocket = acceptSocket;
    provider->closeSocket = closeSocket;
    provider->connectSocket = connectSocket;
    provider->createSocket = createSocket;
    provider->disconnectSocket = disconnectSocket;
    provider->flushSocket = flushSocket;
    provider->listenSocket = listenSocket;
    provider->readSocket = readSocket;
    provider->writeSocket = writeSocket;
    return provider;
}


void mprSetSecureProvider(MprCtx ctx, MprSocketProvider *provider)
{
    mprGetMpr(ctx)->socketService->secureProvider = provider;
}


bool mprHasSecureSockets(MprCtx ctx)
{
    return (mprGetMpr(ctx)->socketService->secureProvider != 0);
}


int mprSetMaxSocketClients(MprCtx ctx, int max)
{
    MprSocketService    *ss;

    mprAssert(ctx);
    mprAssert(max >= 0);

    ss = mprGetMpr(ctx)->socketService;
    ss->maxClients = max;
    return 0;
}


/*
 *  Create a new socket
 */
static MprSocket *createSocket(MprCtx ctx, struct MprSsl *ssl)
{
    MprSocket       *sp;

    sp = mprAllocObjWithDestructorZeroed(ctx, MprSocket, socketDestructor);
    if (sp == 0) {
        return 0;
    }

    sp->handlerPriority = MPR_NORMAL_PRIORITY;
    sp->port = -1;
    sp->fd = -1;
    sp->flags = 0;

    sp->provider = mprGetMpr(ctx)->socketService->standardProvider;
    sp->service = mprGetMpr(ctx)->socketService;

#if BLD_FEATURE_MULTITHREAD
    sp->mutex = mprCreateLock(sp);
#endif
    return sp;
}


/*
 *  Create a new socket
 */
MprSocket *mprCreateSocket(MprCtx ctx, struct MprSsl *ssl)
{
    MprSocketService    *ss;
    MprSocket           *sp;

    ss = mprGetMpr(ctx)->socketService;

    if (ssl) {
#if !BLD_FEATURE_SSL
        return 0;
#endif
        if (ss->secureProvider == NULL || ss->secureProvider->createSocket == NULL) {
            return 0;
        }
        sp = ss->secureProvider->createSocket(ctx, ssl);

    } else {
        mprAssert(ss->standardProvider->createSocket);
        sp = ss->standardProvider->createSocket(ctx, NULL);
    }
    sp->service = ss;
    return sp;
}


static int socketDestructor(MprSocket *sp)
{
    MprSocketService    *ss;

    ss = sp->service;

    mprLock(ss->mutex);
    if (sp->fd >= 0) {
        mprCloseSocket(sp, 1);
    }
    mprUnlock(ss->mutex);
    return 0;
}


/*
 *  Re-initialize all socket variables so the Socket can be reused.
 */
static void resetSocket(MprSocket *sp)
{
    if (sp->fd >= 0) {
        mprCloseSocket(sp, 0);
    }
    if (sp->flags & MPR_SOCKET_CLOSED) {
        sp->acceptCallback = 0;
        sp->acceptData = 0;
        sp->waitForEvents = 0;
        sp->currentEvents = 0;
        sp->error = 0;
        sp->flags = 0;
        sp->ioCallback = 0;
        sp->ioData = 0;
        sp->handlerMask = 0;
        sp->handlerPriority = MPR_NORMAL_PRIORITY;
        sp->interestEvents = 0;
        sp->port = -1;
        sp->fd = -1;
        mprFree(sp->ipAddr);
        sp->ipAddr = 0;
    }
    mprAssert(sp->provider);
}


/*
 *  Open a server socket connection
 */
int mprOpenServerSocket(MprSocket *sp, cchar *host, int port, MprSocketAcceptProc acceptFn, void *data, int flags)
{
    if (sp->provider == 0) {
        return MPR_ERR_NOT_INITIALIZED;
    }
    return sp->provider->listenSocket(sp, host, port, acceptFn, data, flags);
}


static int listenSocket(MprSocket *sp, cchar *host, int port, MprSocketAcceptProc acceptFn, void *data, int initialFlags)
{
    struct sockaddr     *addr;
    socklen_t           addrlen;
    int                 datagram, family, rc;

    lock(sp);

    if (host == 0 || *host == '\0') {
        mprLog(sp, 6, "mprSocket: openServer *:%d, flags %x", port, initialFlags);
    } else {
        mprLog(sp, 6, "mprSocket: openServer %s:%d, flags %x", host, port, initialFlags);
    }

    resetSocket(sp);

    sp->ipAddr = mprStrdup(sp, host);
    sp->port = port;
    sp->acceptCallback = acceptFn;
    sp->acceptData = data;

    sp->flags = (initialFlags &
        (MPR_SOCKET_BROADCAST | MPR_SOCKET_DATAGRAM | MPR_SOCKET_BLOCK |
         MPR_SOCKET_LISTENER | MPR_SOCKET_NOREUSE | MPR_SOCKET_NODELAY | MPR_SOCKET_THREAD));

    datagram = sp->flags & MPR_SOCKET_DATAGRAM;

    if (getSocketInfo(sp, host, port, &family, &addr, &addrlen) < 0) {
        return MPR_ERR_NOT_FOUND;
    }

    /*
     *  Create the O/S socket
     */
    sp->fd = (int) socket(family, datagram ? SOCK_DGRAM: SOCK_STREAM, 0);
    if (sp->fd < 0) {
        unlock(sp);
        return MPR_ERR_CANT_OPEN;
    }

#if !BLD_WIN_LIKE && !VXWORKS
    /*
     *  Children won't inherit this fd
     */
    fcntl(sp->fd, F_SETFD, FD_CLOEXEC);
#endif

#if BLD_UNIX_LIKE
    if (!(sp->flags & MPR_SOCKET_NOREUSE)) {
        rc = 1;
        setsockopt(sp->fd, SOL_SOCKET, SO_REUSEADDR, (char*) &rc, sizeof(rc));
    }
#endif

    rc = bind(sp->fd, addr, addrlen);
    if (rc < 0) {
        rc = errno;
        mprFree(addr);
        closesocket(sp->fd);
        sp->fd = -1;
        unlock(sp);
        return MPR_ERR_CANT_OPEN;
    }
    mprFree(addr);

    if (! datagram) {
        sp->flags |= MPR_SOCKET_LISTENER;
        if (listen(sp->fd, SOMAXCONN) < 0) {
            mprLog(sp, 3, "Listen error %d", mprGetOsError());
            closesocket(sp->fd);
            sp->fd = -1;
            unlock(sp);
            return MPR_ERR_CANT_OPEN;
        }
        sp->handlerMask |= MPR_SOCKET_READABLE;
        sp->handler = mprCreateWaitHandler(sp, sp->fd, MPR_SOCKET_READABLE, (MprWaitProc) acceptProc, sp, 
            sp->handlerPriority, (sp->flags & MPR_SOCKET_THREAD) ? MPR_WAIT_THREAD : 0);
    }

#if BLD_WIN_LIKE
    /*
     *  Delay setting reuse until now so that we can be assured that we have exclusive use of the port.
     */
    if (!(sp->flags & MPR_SOCKET_NOREUSE)) {
        rc = 1;
        setsockopt(sp->fd, SOL_SOCKET, SO_REUSEADDR, (char*) &rc, sizeof(rc));
    }
#endif

    mprSetSocketBlockingMode(sp, (bool) (sp->flags & MPR_SOCKET_BLOCK));

    /*
     *  TCP/IP stacks have the No delay option (nagle algorithm) on by default.
     */
    if (sp->flags & MPR_SOCKET_NODELAY) {
        mprSetSocketNoDelay(sp, 1);
    }
    unlock(sp);
    return sp->fd;
}


/*
 *  Open a client socket connection
 */
int mprOpenClientSocket(MprSocket *sp, cchar *host, int port, int flags)
{
    if (sp->provider == 0) {
        return MPR_ERR_NOT_INITIALIZED;
    }
    return sp->provider->connectSocket(sp, host, port, flags);
}


static int connectSocket(MprSocket *sp, cchar *host, int port, int initialFlags)
{
    struct sockaddr     *addr;
    socklen_t           addrlen;
    int                 broadcast, datagram, family, rc, err;

    lock(sp);

    resetSocket(sp);

    mprLog(sp, 6, "openClient: %s:%d, flags %x", host, port, initialFlags);

    sp->port = port;
    sp->flags = (initialFlags &
        (MPR_SOCKET_BROADCAST | MPR_SOCKET_DATAGRAM | MPR_SOCKET_BLOCK |
         MPR_SOCKET_LISTENER | MPR_SOCKET_NOREUSE | MPR_SOCKET_NODELAY | MPR_SOCKET_THREAD));
    sp->flags |= MPR_SOCKET_CLIENT;

    mprFree(sp->ipAddr);
    sp->ipAddr = mprStrdup(sp, host);

    broadcast = sp->flags & MPR_SOCKET_BROADCAST;
    if (broadcast) {
        sp->flags |= MPR_SOCKET_DATAGRAM;
    }
    datagram = sp->flags & MPR_SOCKET_DATAGRAM;

    if (getSocketInfo(sp, host, port, &family, &addr, &addrlen) < 0) {
        err = mprGetSocketError(sp);
        closesocket(sp->fd);
        sp->fd = -1;
        unlock(sp);
        return MPR_ERR_CANT_ACCESS;
    }
    /*
     *  Create the O/S socket
     */
    sp->fd = (int) socket(family, datagram ? SOCK_DGRAM: SOCK_STREAM, 0);
    if (sp->fd < 0) {
        err = mprGetSocketError(sp);
        unlock(sp);
        return MPR_ERR_CANT_OPEN;
    }

#if !BLD_WIN_LIKE && !VXWORKS
    /*
     *  Children should not inherit this fd
     */
    fcntl(sp->fd, F_SETFD, FD_CLOEXEC);
#endif

    if (broadcast) {
        int flag = 1;
        if (setsockopt(sp->fd, SOL_SOCKET, SO_BROADCAST, (char *) &flag, sizeof(flag)) < 0) {
            err = mprGetSocketError(sp);
            closesocket(sp->fd);
            sp->fd = -1;
            unlock(sp);
            return MPR_ERR_CANT_INITIALIZE;
        }
    }

    if (!datagram) {
        sp->flags |= MPR_SOCKET_CONNECTING;
        do {
            rc = connect(sp->fd, addr, addrlen);
        } while (rc == -1 && errno == EINTR);
        err = errno;
        if (rc < 0) {
            /* MAC/BSD returns EADDRINUSE */
            if (errno == EINPROGRESS || errno == EALREADY || errno == EADDRINUSE) {
#if MACOSX || LINUX
                do {
                    struct pollfd pfd;
                    pfd.fd = sp->fd;
                    pfd.events = POLLOUT;
                    rc = poll(&pfd, 1, MPR_TIMEOUT_SOCKETS);
                } while (rc < 0 && errno == EINTR);
#endif
                if (rc > 0) {
                    errno = EISCONN;
                }
            } 
            if (errno != EISCONN) {
                err = mprGetSocketError(sp);
                closesocket(sp->fd);
                sp->fd = -1;
                unlock(sp);
                mprFree(addr);
                return MPR_ERR_CANT_COMPLETE;
            }
        }
    }
    mprFree(addr);

    mprSetSocketBlockingMode(sp, (bool) (sp->flags & MPR_SOCKET_BLOCK));

    /*
     *  TCP/IP stacks have the no delay option (nagle algorithm) on by default.
     */
    if (sp->flags & MPR_SOCKET_NODELAY) {
        mprSetSocketNoDelay(sp, 1);
    }

    unlock(sp);
    return sp->fd;
}


/*
 *  Abortive disconnect. Thread-safe. (e.g. from a timeout or callback thread). This closes the underlying socket file
 *  descriptor but keeps the handler and socket object intact.
 */
void mprDisconnectSocket(MprSocket *sp)
{
    mprAssert(sp);
    mprAssert(sp->provider);

    if (sp->provider) {
        sp->provider->disconnectSocket(sp);
    }
}


static void disconnectSocket(MprSocket *sp)
{
    char    buf[16];

    /*
     *  Defensive lock buster. Use try lock incase an operation is blocked somewhere with a lock asserted. 
     *  Should never happen.
     */
    if (!mprTryLock(sp->mutex)) {
        return;
    }
    if (sp->fd >= 0 && !(sp->flags & MPR_SOCKET_EOF)) {
        /*
         *  Read any outstanding read data to minimize resets. Then do a shutdown to send a FIN and read 
         *  outstanding data.  All non-blocking.
         */
        mprSetSocketBlockingMode(sp, 0);
        while (recv(sp->fd, buf, sizeof(buf), 0) > 0) {
            ;
        }
        shutdown(sp->fd, SHUT_RDWR);
        sp->flags |= MPR_SOCKET_EOF;
        if (sp->handler) {
            mprRecallWaitHandler(sp->handler);
        }
    }
    unlock(sp);
}


/*
 *  Close a socket
 */
void mprCloseSocket(MprSocket *sp, bool gracefully)
{
    mprAssert(sp);
    mprAssert(sp->provider);

    if (sp->provider == 0) {
        return;
    }
    sp->provider->closeSocket(sp, gracefully);
}


/*
 *  Standard (non-SSL) close. Permit multiple calls.
 */
static void closeSocket(MprSocket *sp, bool gracefully)
{
    MprSocketService    *ss;
    MprWaitService      *waitService;
    MprTime             timesUp;
    char                buf[16];

    waitService = mprGetMpr(sp)->waitService;
    ss = mprGetMpr(sp)->socketService;

    lock(sp);

    if (sp->flags & MPR_SOCKET_CLOSED) {
        unlock(sp);
        return;
    }
    sp->flags |= MPR_SOCKET_CLOSED | MPR_SOCKET_EOF;

    if (sp->handler) {
        mprFree(sp->handler);
        sp->handler = 0;
    }

    if (sp->fd >= 0) {
        /*
         *  Read any outstanding read data to minimize resets. Then do a shutdown to send a FIN and read outstanding 
         *  data. All non-blocking.
         */
        if (gracefully) {
            mprSetSocketBlockingMode(sp, 0);
            while (recv(sp->fd, buf, sizeof(buf), 0) > 0) {
                ;
            }
        }
        if (shutdown(sp->fd, SHUT_RDWR) == 0) {
            if (gracefully) {
                timesUp = mprGetTime(0) + MPR_TIMEOUT_LINGER;
                do {
                    if (recv(sp->fd, buf, sizeof(buf), 0) <= 0) {
                        break;
                    }
                } while (mprGetTime(0) < timesUp);
            }
        }
        closesocket(sp->fd);
        sp->fd = -1;
    }

    if (! (sp->flags & (MPR_SOCKET_LISTENER | MPR_SOCKET_CLIENT))) {
        mprLock(ss->mutex);
        if (--ss->numClients < 0) {
            ss->numClients = 0;
        }
        mprUnlock(ss->mutex);
    }
    unlock(sp);
}


/*
 *  Accept wait handler. May be called directly if single-threaded or on a worker thread.
 */
static int acceptProc(MprSocket *listen, int mask)
{
    if (listen->provider) {
        listen->provider->acceptSocket(listen, 1);
        return 0;
    }
    return 0;
}


/*
 *  Standard accept
 */
static MprSocket *acceptSocket(MprSocket *listen, bool invokeCallback)
{
    MprSocketService            *ss;
    MprSocket                   *nsp;
    struct sockaddr_storage     addrStorage;
    struct sockaddr             *addr;
    char                        clientIpAddr[MPR_MAX_IP_ADDR];
    socklen_t                   addrlen;
    int                         fd, port;

    if (listen->acceptCallback == 0) {
        return 0;
    }

    ss = mprGetMpr(listen)->socketService;
    addr = (struct sockaddr*) &addrStorage;
    addrlen = sizeof(addrStorage);

    fd = (int) accept(listen->fd, addr, &addrlen);
    if (fd < 0) {
        if (mprGetError() != EAGAIN) {
            mprLog(listen, 1, "socket: accept failed, errno %d", mprGetOsError());
        }
        mprEnableSocketEvents(listen);
        return 0;
    }
    mprAssert(addrlen <= sizeof(addrStorage));

    nsp = mprCreateSocket(ss, listen->ssl);
    if (nsp == 0) {
        closesocket(fd);
        mprEnableSocketEvents(listen);
        return 0;
    }
    nsp->fd = fd;

    /*
     *  Limit the number of simultaneous clients
     */
    mprLock(ss->mutex);
    if (++ss->numClients >= ss->maxClients) {
        mprUnlock(ss->mutex);
        mprLog(listen, 1, "Rejecting connection, too many client connections (%d)", ss->numClients);
        mprFree(nsp);
        mprEnableSocketEvents(listen);
        return 0;
    }
    mprUnlock(ss->mutex);

#if !BLD_WIN_LIKE && !VXWORKS
    fcntl(fd, F_SETFD, FD_CLOEXEC);     /* Prevent children inheriting this socket */
#endif

    nsp->ipAddr = listen->ipAddr;
    nsp->acceptData = listen->acceptData;
    nsp->ioData = listen->ioData;
    nsp->port = listen->port;
    nsp->acceptCallback = listen->acceptCallback;
    nsp->flags = listen->flags;
    nsp->flags &= ~MPR_SOCKET_LISTENER;
    nsp->listenSock = listen;

    mprSetSocketBlockingMode(nsp, (nsp->flags & MPR_SOCKET_BLOCK) ? 1: 0);

    if (nsp->flags & MPR_SOCKET_NODELAY) {
        mprSetSocketNoDelay(nsp, 1);
    }

    if (getSocketIpAddr(nsp, addr, addrlen, clientIpAddr, sizeof(clientIpAddr), &port) != 0) {
        mprAssert(0);
        mprFree(nsp);
        mprEnableSocketEvents(listen);
        return 0;
    }
    nsp->clientIpAddr = mprStrdup(nsp, clientIpAddr);

    if (invokeCallback) {
        /*
         *  Call the user accept callback. We do not remember the socket handle, it is up to the callback to manage it
         *  from here on. The callback can delete the socket.
         */
        if (nsp->acceptCallback) {
            if ((nsp->acceptCallback)(nsp, nsp->acceptData, clientIpAddr, port) != 0) {
                return 0;
            }
        } else {
            mprFree(nsp);
            mprEnableSocketEvents(listen);
            return 0;
        }
    }
    return nsp;
}


/*
 *  Read data. Return zero for EOF or no data if in non-blocking mode. Return -1 for errors. On success,
 *  return the number of bytes read. Use getEof to tell if we are EOF or just no data (in non-blocking mode).
 */
int mprReadSocket(MprSocket *sp, void *buf, int bufsize)
{
    mprAssert(sp);
    mprAssert(buf);
    mprAssert(bufsize > 0);
    mprAssert(sp->provider);

    if (sp->provider == 0) {
        return MPR_ERR_NOT_INITIALIZED;
    }
    return sp->provider->readSocket(sp, buf, bufsize);
}


/*
 *  Standard read from a socket (Non SSL)
 */
static int readSocket(MprSocket *sp, void *buf, int bufsize)
{
    struct sockaddr_storage server;
    socklen_t               len;
    int                     bytes, errCode;

    mprAssert(buf);
    mprAssert(bufsize > 0);
    mprAssert(~(sp->flags & MPR_SOCKET_CLOSED));

    lock(sp);

    if (sp->flags & MPR_SOCKET_EOF) {
        unlock(sp);
        return 0;
    }
again:
    if (sp->flags & MPR_SOCKET_DATAGRAM) {
        len = sizeof(server);
        bytes = recvfrom(sp->fd, buf, bufsize, MSG_NOSIGNAL, (struct sockaddr*) &server, (socklen_t*) &len);
    } else {
        bytes = recv(sp->fd, buf, bufsize, MSG_NOSIGNAL);
    }

    if (bytes < 0) {
        errCode = mprGetSocketError(sp);
        if (errCode == EINTR) {
            goto again;

        } else if (errCode == EAGAIN || errCode == EWOULDBLOCK) {
            bytes = 0;                          /* No data available */

        } else if (errCode == ECONNRESET) {
            sp->flags |= MPR_SOCKET_EOF;        /* Disorderly disconnect */
            bytes = 0;

        } else {
            sp->flags |= MPR_SOCKET_EOF;        /* Some other error */
            bytes = -errCode;
        }

    } else if (bytes == 0) {                    /* EOF */
        sp->flags |= MPR_SOCKET_EOF;
    }

#if KEEP && FOR_SSL
    /*
     *  If there is more buffered data to read, then ensure the handler recalls us again even if there is no more IO events.
     */
    if (isBufferedData()) {
        if (sp->handler) {
            mprRecallWaitHandler(sp->handler);
        }
    }
#endif
    unlock(sp);
    return bytes;
}


/*
 *  Write data. Return the number of bytes written or -1 on errors. NOTE: this routine will return with a
 *  short write if the underlying socket can't accept any more data.
 */
int mprWriteSocket(MprSocket *sp, void *buf, int bufsize)
{
    mprAssert(sp);
    mprAssert(buf);
    mprAssert(bufsize > 0);
    mprAssert(sp->provider);

    if (sp->provider == 0) {
        return MPR_ERR_NOT_INITIALIZED;
    }
    return sp->provider->writeSocket(sp, buf, bufsize);
}


/*
 *  Standard write to a socket (Non SSL)
 */
static int writeSocket(MprSocket *sp, void *buf, int bufsize)
{
    struct sockaddr     *addr;
    socklen_t           addrlen;
    int                 family, sofar, errCode, len, written;

    mprAssert(buf);
    mprAssert(bufsize >= 0);
    mprAssert((sp->flags & MPR_SOCKET_CLOSED) == 0);

    lock(sp);

    if (sp->flags & (MPR_SOCKET_BROADCAST | MPR_SOCKET_DATAGRAM)) {
        if (getSocketInfo(sp, sp->ipAddr, sp->port, &family, &addr, &addrlen) < 0) {
            unlock(sp);
            return MPR_ERR_NOT_FOUND;
        }
    }
    if (sp->flags & MPR_SOCKET_EOF) {
        sofar = MPR_ERR_CANT_WRITE;
    } else {
        errCode = 0;
        len = bufsize;
        sofar = 0;
        while (len > 0) {
            unlock(sp);
            if ((sp->flags & MPR_SOCKET_BROADCAST) || (sp->flags & MPR_SOCKET_DATAGRAM)) {
                written = sendto(sp->fd, &((char*) buf)[sofar], len, MSG_NOSIGNAL, addr, addrlen);
            } else {
                written = send(sp->fd, &((char*) buf)[sofar], len, MSG_NOSIGNAL);
            }
            lock(sp);

            if (written < 0) {
                errCode = mprGetSocketError(sp);
                if (errCode == EINTR) {
                    continue;
                } else if (errCode == EAGAIN || errCode == EWOULDBLOCK) {
#if BLD_WIN_LIKE
                    /*
                     *  Windows sockets don't support blocking I/O. So we simulate here
                     */
                    if (sp->flags & MPR_SOCKET_BLOCK) {
                        mprSleep(sp, 0);
                        continue;
                    }
#endif
                    unlock(sp);
                    return sofar;
                }
                unlock(sp);
                return -errCode;
            }
            len -= written;
            sofar += written;
        }
    }
    unlock(sp);
    return sofar;
}


/*
 *  Write a string to the socket
 */
int mprWriteSocketString(MprSocket *sp, cchar *str)
{
    return mprWriteSocket(sp, (void*) str, (int) strlen(str));
}


int mprWriteSocketVector(MprSocket *sp, MprIOVec *iovec, int count)
{
    char    *start;
    int     total, len, i, written;

#if BLD_UNIX_LIKE
    if (sp->ssl == 0) {
        return writev(sp->fd, (const struct iovec*) iovec, count);
    } else
#endif
    {
        if (count <= 0) {
            return 0;
        }

        start = iovec[0].start;
        len = (int) iovec[0].len;
        mprAssert(len > 0);

        for (total = i = 0; i < count; ) {
            written = mprWriteSocket(sp, start, len);
            if (written < 0) {
                return written;

            } else if (written == 0) {
                break;

            } else {
                len -= written;
                start += written;
                total += written;
                if (len <= 0) {
                    i++;
                    start = iovec[i].start;
                    len = (int) iovec[i].len;
                }
            }
        }
        return total;
    }
}


#if !BLD_FEATURE_ROMFS
#if !LINUX || __UCLIBC__
static int localSendfile(MprSocket *sp, MprFile *file, MprOffset offset, int len)
{
    char    buf[MPR_BUFSIZE];

    mprSeek(file, SEEK_SET, (int) offset);
    len = min(len, sizeof(buf));
    if ((len = mprRead(file, buf, len)) < 0) {
        mprAssert(0);
        return MPR_ERR_CANT_READ;
    }
    return mprWriteSocket(sp, buf, len);
}
#endif


/*
 *  Write data from a file to a socket. Includes the ability to write header before and after the file data.
 *  Works even with a null "file" to just output the headers.
 */
MprOffset mprSendFileToSocket(MprSocket *sock, MprFile *file, MprOffset offset, int bytes, MprIOVec *beforeVec, 
    int beforeCount, MprIOVec *afterVec, int afterCount)
{
#if MACOSX && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
    struct sf_hdtr  def;
#endif
    off_t           written, off;
    int             rc, i, done, toWriteBefore, toWriteAfter, toWriteFile;

    rc = 0;

#if MACOSX && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1050
    written = bytes;
    def.hdr_cnt = beforeCount;
    def.headers = (beforeCount > 0) ? (struct iovec*) beforeVec: 0;
    def.trl_cnt = afterCount;
    def.trailers = (afterCount > 0) ? (struct iovec*) afterVec: 0;

    if (file && file->fd >= 0) {
        rc = sendfile(file->fd, sock->fd, offset, &written, &def, 0);
    } else
#else
    if (1) 
#endif
    {
        /*
         *  Either !MACOSX or no file is opened
         */
        done = written = 0;
        for (i = toWriteBefore = 0; i < beforeCount; i++) {
            toWriteBefore += (int) beforeVec[i].len;
        }
        for (i = toWriteAfter = 0; i < afterCount; i++) {
            toWriteAfter += (int) afterVec[i].len;
        }
        toWriteFile = bytes - toWriteBefore - toWriteAfter;
        mprAssert(toWriteFile >= 0);

        /*
         *  Linux sendfile does not have the integrated ability to send headers. Must do it separately here.
         *  I/O requests may return short (write fewer than requested bytes).
         */
        if (beforeCount > 0) {
            rc = mprWriteSocketVector(sock, beforeVec, beforeCount);
            if (rc > 0) {
                written += rc;
            }
            if (rc != toWriteBefore) {
                done++;
            }
        }

        if (!done && toWriteFile > 0) {
            off = (off_t) offset;
#if LINUX && !__UCLIBC__
            rc = sendfile(sock->fd, file->fd, &off, toWriteFile);
#else
            rc = localSendfile(sock, file, offset, toWriteFile);
#endif
            if (rc > 0) {
                written += rc;
                if (rc != toWriteFile) {
                    done++;
                }
            }
        }
        if (!done && afterCount > 0) {
            rc = mprWriteSocketVector(sock, afterVec, afterCount);
            if (rc > 0) {
                written += rc;
            }
        }
    }

    if (rc < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return written;
        }
        return -1;
    }
    return written;
}
#endif /* !BLD_FEATURE_ROMFS */


static int flushSocket(MprSocket *sp)
{
    return 0;
}


int mprFlushSocket(MprSocket *sp)
{
    if (sp->provider == 0) {
        return MPR_ERR_NOT_INITIALIZED;
    }
    return sp->provider->flushSocket(sp);
}


bool mprHasSocketPendingData(MprSocket *sp)
{
    return (sp->flags & MPR_SOCKET_PENDING) ? 1 : 0;
}

/*
 *  Return true if end of file
 */
bool mprIsSocketEof(MprSocket *sp)
{
    return ((sp->flags & MPR_SOCKET_EOF) != 0);
}


/*
 *  Set the EOF condition
 */
void mprSetSocketEof(MprSocket *sp, bool eof)
{
    if (eof) {
        sp->flags |= MPR_SOCKET_EOF;
    } else {
        sp->flags &= ~MPR_SOCKET_EOF;
    }
}


/*
 *  Define an IO callback for this socket. The callback called whenever there is an event of interest as
 *  defined by handlerMask (MPR_SOCKET_READABLE, ...)
 *  If a handler is already defined, just enable events.
 */
void mprSetSocketCallback(MprSocket *sp, MprSocketProc fn, void *data, int handlerMask, int pri)
{
    lock(sp);
    sp->handlerMask = handlerMask;
    if (sp->handler == 0) {
        sp->ioCallback = fn;
        sp->ioData = data;
        sp->handlerPriority = pri;
        sp->handler = mprCreateWaitHandler(sp, sp->fd, sp->handlerMask, (MprWaitProc) ioProc, sp, sp->handlerPriority, 
            (sp->flags & MPR_SOCKET_THREAD) ? MPR_WAIT_THREAD : 0);
    } else {
        mprSetWaitEvents(sp->handler, handlerMask, -1);
    }
    unlock(sp);
}


/*
 *  Define the events of interest. Must only be called with a locked socket.
 */
void mprSetSocketEventMask(MprSocket *sp, int handlerMask)
{
    lock(sp);
    sp->handlerMask = handlerMask;
    if (handlerMask) {
        if (sp->handler) {
            mprSetWaitEvents(sp->handler, handlerMask, -1);

        } else {
            sp->handler = mprCreateWaitHandler(sp, sp->fd, handlerMask, (MprWaitProc) ioProc, sp, sp->handlerPriority, 
                (sp->flags & MPR_SOCKET_THREAD) ? MPR_WAIT_THREAD : 0);
        }

    } else if (sp->handler) {
        mprSetWaitEvents(sp->handler, handlerMask, -1);
    }
    unlock(sp);
}


void mprEnableSocketEvents(MprSocket *sp)
{
    mprAssert(sp);

    lock(sp);
    if (sp->handler) {
        mprEnableWaitEvents(sp->handler);
    }
    unlock(sp);
}


void mprDisableSocketEvents(MprSocket *sp)
{
    mprAssert(sp);

    lock(sp);
    if (sp->handler) {
        mprDisableWaitEvents(sp->handler);
    }
    unlock(sp);
}

/*
 *  Return the O/S socket file handle
 */
int mprGetSocketFd(MprSocket *sp)
{
    return sp->fd;
}


/*
 *  Return the blocking mode of the socket
 */
bool mprGetSocketBlockingMode(MprSocket *sp)
{
    return sp->flags & MPR_SOCKET_BLOCK;
}


/*
 *  Get the socket flags
 */
int mprGetSocketFlags(MprSocket *sp)
{
    return sp->flags;
}


/*
 *  Set whether the socket blocks or not on read/write
 */
int mprSetSocketBlockingMode(MprSocket *sp, bool on)
{
    int     flag, oldMode;

    lock(sp);
    oldMode = sp->flags & MPR_SOCKET_BLOCK;

    sp->flags &= ~(MPR_SOCKET_BLOCK);
    if (on) {
        sp->flags |= MPR_SOCKET_BLOCK;
    }

    flag = (sp->flags & MPR_SOCKET_BLOCK) ? 0 : 1;

#if BLD_WIN_LIKE
    ioctlsocket(sp->fd, FIONBIO, (ulong*) &flag);
#elif VXWORKS
    ioctl(sp->fd, FIONBIO, (int) &flag);
#else
    flag = 0;
    if (on) {
        fcntl(sp->fd, F_SETFL, fcntl(sp->fd, F_GETFL) & ~O_NONBLOCK);
    } else {
        fcntl(sp->fd, F_SETFL, fcntl(sp->fd, F_GETFL) | O_NONBLOCK);
    }
#endif
    unlock(sp);
    return oldMode;
}


/*
 *  Set the TCP delay behavior (nagle algorithm)
 */
int mprSetSocketNoDelay(MprSocket *sp, bool on)
{
    int     oldDelay;

    lock(sp);

    oldDelay = sp->flags & MPR_SOCKET_NODELAY;
    if (on) {
        sp->flags |= MPR_SOCKET_NODELAY;
    } else {
        sp->flags &= ~(MPR_SOCKET_NODELAY);
    }
#if BLD_WIN_LIKE
    {
        BOOL    noDelay;
        noDelay = on ? 1 : 0;
        setsockopt(sp->fd, IPPROTO_TCP, TCP_NODELAY, (FAR char*) &noDelay, sizeof(BOOL));
    }
#else
    {
        int     noDelay;
        noDelay = on ? 1 : 0;
        setsockopt(sp->fd, IPPROTO_TCP, TCP_NODELAY, (char*) &noDelay, sizeof(int));
    }
#endif /* BLD_WIN_LIKE */

    unlock(sp);
    return oldDelay;
}


/*
 *  Get the port number
 */
int mprGetSocketPort(MprSocket *sp)
{
    return sp->port;
}


/*
 *  IO ready handler. May be called directly if single-threaded or on a worker thread.
 */
static int ioProc(MprSocket *sp, int mask)
{
    int     rc;

    lock(sp);
    if (sp->ioCallback == 0 || (sp->handlerMask & mask) == 0) {
        mprAssert(sp->handlerMask & mask);
        if ((sp->handlerMask & mask) == 0) {
            mprLog(sp, 0, "ioProc: Spurious event. callback %x, handlerMask %x mask %x", sp->ioCallback, 
                sp->handlerMask, mask);
        }
        unlock(sp);
        return 0;
    }
    mask &= sp->handlerMask;
    if (sp->flags & MPR_SOCKET_RUNNING) {
        return 0;
    }
    sp->flags |= MPR_SOCKET_RUNNING;
    unlock(sp);

    rc = (sp->ioCallback)(sp->ioData, mask);

    if (rc == 0) {
        lock(sp);
        sp->flags &= ~MPR_SOCKET_RUNNING;
        unlock(sp);
    }
    return rc;
}


/*
 *  Map the O/S error code to portable error codes.
 */
int mprGetSocketError(MprSocket *sp)
{
#if BLD_WIN_LIKE
    int     rc;
    switch (rc = WSAGetLastError()) {
    case WSAEINTR:
        return EINTR;

    case WSAENETDOWN:
        return ENETDOWN;

    case WSAEWOULDBLOCK:
        return EWOULDBLOCK;

    case WSAEPROCLIM:
        return EAGAIN;

    case WSAECONNRESET:
    case WSAECONNABORTED:
        return ECONNRESET;

    case WSAECONNREFUSED:
        return ECONNREFUSED;

    case WSAEADDRINUSE:
        return EADDRINUSE;
    default:
        return EINVAL;
    }
#else
    return errno;
#endif
}


#if BLD_HAS_GETADDRINFO
/*
 *  Get a socket address from a host/port combination. If a host provides both IPv4 and IPv6 addresses, 
 *  prefer the IPv4 address.
 */
static int getSocketInfo(MprCtx ctx, cchar *host, int port, int *family, struct sockaddr **addr, socklen_t *addrlen)
{
    MprSocketService    *ss;
    struct addrinfo     hints, *res;
    char                portBuf[MPR_MAX_IP_PORT];
    int                 rc;

    mprAssert(ctx);
    mprAssert(host);
    mprAssert(addr);

    ss = mprGetMpr(ctx)->socketService;

    mprLock(ss->mutex);
    memset((char*) &hints, '\0', sizeof(hints));

    /*
     *  Note that IPv6 does not support broadcast, there is no 255.255.255.255 equivalent.
     *  Multicast can be used over a specific link, but the user must provide that address plus %scope_id.
     */
    if (host == 0 || strcmp(host, "") == 0) {
        host = 0;
        hints.ai_flags |= AI_PASSIVE;           /* Bind to 0.0.0.0 and :: */
    }
    hints.ai_socktype = SOCK_STREAM;

    mprItoa(portBuf, sizeof(portBuf), port, 10);

    hints.ai_family = AF_INET;
    res = 0;

    /*
     *  Try to sleuth the address to avoid duplicate address lookups. Then try IPv4 first then IPv6.
     */
    rc = -1;
    if (host == NULL || strchr(host, ':') == 0) {
        /* 
         *  Looks like IPv4. Map localhost to 127.0.0.1 to avoid crash bug in MAC OS X.
         */
        if (host && strcmp(host, "localhost") == 0) {
            host = "127.0.0.1";
        }
        rc = getaddrinfo(host, portBuf, &hints, &res);
    }
    if (rc != 0) {
        hints.ai_family = AF_INET6;
        rc = getaddrinfo(host, portBuf, &hints, &res);
        if (rc != 0) {
            mprUnlock(ss->mutex);
            return MPR_ERR_CANT_OPEN;
        }
    }

    *addr = (struct sockaddr*) mprAllocObjZeroed(ctx, struct sockaddr_storage);
    mprMemcpy((char*) *addr, sizeof(struct sockaddr_storage), (char*) res->ai_addr, (int) res->ai_addrlen);

    *addrlen = (int) res->ai_addrlen;
    *family = res->ai_family;

    freeaddrinfo(res);
    mprUnlock(ss->mutex);
    return 0;
}


#elif MACOSX
static int getSocketInfo(MprCtx ctx, cchar *host, int port, int *family, struct sockaddr **addr, socklen_t *addrlen)
{
    MprSocketService    *ss;
    struct hostent      *hostent;
    struct sockaddr_in  *sa;
    struct sockaddr_in6 *sa6;
    int                 len, err;

    mprAssert(addr);
    ss = mprGetMpr(ctx)->socketService;

    mprLock(ss->mutex);
    len = sizeof(struct sockaddr_in);
    if ((hostent = getipnodebyname(host, AF_INET, 0, &err)) == NULL) {
        len = sizeof(struct sockaddr_in6);
        if ((hostent = getipnodebyname(host, AF_INET6, 0, &err)) == NULL) {
            mprUnlock(ss->mutex);
            return MPR_ERR_CANT_OPEN;
        }
        sa6 = (struct sockaddr_in6*) mprAllocZeroed(ctx, len);
        if (sa6 == 0) {
            mprUnlock(ss->mutex);
            return MPR_ERR_NO_MEMORY;
        }
        memcpy((char*) &sa6->sin6_addr, (char*) hostent->h_addr_list[0], (size_t) hostent->h_length);
        sa6->sin6_family = hostent->h_addrtype;
        sa6->sin6_port = htons((short) (port & 0xFFFF));
        *addr = (struct sockaddr*) sa6;

    } else {
        sa = (struct sockaddr_in*) mprAllocZeroed(ctx, len);
        if (sa == 0) {
            mprUnlock(ss->mutex);
            return MPR_ERR_NO_MEMORY;
        }
        memcpy((char*) &sa->sin_addr, (char*) hostent->h_addr_list[0], (size_t) hostent->h_length);
        sa->sin_family = hostent->h_addrtype;
        sa->sin_port = htons((short) (port & 0xFFFF));
        *addr = (struct sockaddr*) sa;
    }

    mprAssert(hostent);
    *addrlen = len;
    *family = hostent->h_addrtype;
    freehostent(hostent);

    mprUnlock(ss->mutex);
    return 0;
}


#else

static int getSocketInfo(MprCtx ctx, cchar *host, int port, int *family, struct sockaddr **addr, socklen_t *addrlen)
{
    MprSocketService    *ss;
    struct sockaddr_in  *sa;

    ss = mprGetMpr(ctx)->socketService;

    sa = mprAllocObjZeroed(ctx, struct sockaddr_in);
    if (sa == 0) {
        return MPR_ERR_NO_MEMORY;
    }

    memset((char*) sa, '\0', sizeof(struct sockaddr_in));
    sa->sin_family = AF_INET;
    sa->sin_port = htons((short) (port & 0xFFFF));

    if (strcmp(host, "") != 0) {
        sa->sin_addr.s_addr = inet_addr((char*) host);
    } else {
        sa->sin_addr.s_addr = INADDR_ANY;
    }

    /*
     *  gethostbyname is not thread safe
     */
    mprLock(ss->mutex);
    if (sa->sin_addr.s_addr == INADDR_NONE) {
#if VXWORKS
        /*
         *  VxWorks only supports one interface and this code only supports IPv4
         */
        sa->sin_addr.s_addr = (ulong) hostGetByName((char*) host);
        if (sa->sin_addr.s_addr < 0) {
            mprUnlock(ss->mutex);
            mprAssert(0);
            return 0;
        }
#else
        struct hostent *hostent;
        hostent = gethostbyname2(host, AF_INET);
        if (hostent == 0) {
            hostent = gethostbyname2(host, AF_INET6);
            if (hostent == 0) {
                mprUnlock(ss->mutex);
                return MPR_ERR_NOT_FOUND;
            }
        }
        memcpy((char*) &sa->sin_addr, (char*) hostent->h_addr_list[0], (size_t) hostent->h_length);
#endif
    }
    *addr = (struct sockaddr*) sa;
    *addrlen = sizeof(struct sockaddr_in);
    *family = sa->sin_family;
    mprUnlock(ss->mutex);

    return 0;
}
#endif


/*
 *  Return a numerical IP address and port for the given socket info
 */
static int getSocketIpAddr(MprCtx ctx, struct sockaddr *addr, int addrlen, char *host, int hostLen, int *port)
{
#if (BLD_UNIX_LIKE || WIN)
    char    service[NI_MAXSERV];

    if (getnameinfo(addr, addrlen, host, hostLen, service, sizeof(service), NI_NUMERICHOST | NI_NUMERICSERV | NI_NOFQDN)) {
        return MPR_ERR_BAD_VALUE;
    }
    *port = atoi(service);

#else
    struct sockaddr_in  *sa;

#if HAVE_NTOA_R
    sa = (struct sockaddr_in*) addr;
    inet_ntoa_r(sa->sin_addr, host, hostLen);
#else
    uchar   *cp;
    sa = (struct sockaddr_in*) addr;
    cp = (uchar*) &sa->sin_addr;
    mprSprintf(host, hostLen, "%d.%d.%d.%d", cp[0], cp[1], cp[2], cp[3]);
#endif
    *port = ntohs(sa->sin_port);
#endif
    return 0;
}


/*
 *  Parse ipAddrPort and return the IP address and port components. Handles ipv4 and ipv6 addresses. When an ipAddrPort
 *  contains an ipv6 port it should be written as
 *
 *      aaaa:bbbb:cccc:dddd:eeee:ffff:gggg:hhhh:iiii
 *  or
 *      [aaaa:bbbb:cccc:dddd:eeee:ffff:gggg:hhhh:iiii]:port
 */
int mprParseIp(MprCtx ctx, cchar *ipAddrPort, char **ipAddrRef, int *port, int defaultPort)
{
    char    *ipAddr;
    char    *cp;
    int     colonCount;

    ipAddr = NULL;
    if (defaultPort < 0) {
        defaultPort = 80;
    }

    /*
     * First check if ipv6 or ipv4 address by looking for > 1 colons.
     */
    colonCount = 0;
    for (cp = (char*) ipAddrPort; ((*cp != '\0') && (colonCount < 2)) ; cp++) {
        if (*cp == ':') {
            colonCount++;
        }
    }

    if (colonCount > 1) {
        /*
         *  IPv6. If port is present, it will follow a closing bracket ']'
         */
        if ((cp = strchr(ipAddrPort, ']')) != 0) {
            cp++;
            if ((*cp) && (*cp == ':')) {
                *port = (*++cp == '*') ? -1 : atoi(cp);

                /* set ipAddr to ipv6 address without brackets */
                ipAddr = mprStrdup(ctx, ipAddrPort+1);
                cp = strchr(ipAddr, ']');
                *cp = '\0';

            } else {
                /* Handles [a:b:c:d:e:f:g:h:i] case (no port)- should not occur */
                ipAddr = mprStrdup(ctx, ipAddrPort+1);
                cp = strchr(ipAddr, ']');
                *cp = '\0';

                /* No port present, use callers default */
                *port = defaultPort;
            }
        } else {
            /* Handles a:b:c:d:e:f:g:h:i case (no port) */
            ipAddr = mprStrdup(ctx, ipAddrPort);

            /* No port present, use callers default */
            *port = defaultPort;
        }

    } else {
        /* 
         *  ipv4 
         */
        ipAddr = mprStrdup(ctx, ipAddrPort);

        if ((cp = strchr(ipAddr, ':')) != 0) {
            *cp++ = '\0';
            if (*cp == '*') {
                *port = -1;
            } else {
                *port = atoi(cp);
            }
            if (*ipAddr == '*') {
                mprFree(ipAddr);
                ipAddr = mprStrdup(ctx, "127.0.0.1");
            }

        } else {
            if (isdigit((int) *ipAddr)) {
                *port = atoi(ipAddr);
                mprFree(ipAddr);
                ipAddr = mprStrdup(ctx, "127.0.0.1");

            } else {
                /* No port present, use callers default */
                *port = defaultPort;
            }
        }
    }

    if (ipAddrRef) {
        *ipAddrRef = ipAddr;
    }

    return 0;
}


bool mprIsSocketSecure(MprSocket *sp)
{
    return sp->sslSocket != 0;
}


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
