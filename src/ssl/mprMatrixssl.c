/*
 *  matrixSslModule.c -- Support for secure sockets via MatrixSSL
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"
#include    "mprSsl.h"

#if BLD_FEATURE_MATRIXSSL
/***************************** Forward Declarations ***************************/

static MprSocket *acceptMss(MprSocket *sp, bool invokeCallback);
static void     closeMss(MprSocket *sp, bool gracefully);
static int      configureMss(MprSsl *ssl);
static int      connectMss(MprSocket *sp, cchar *host, int port, int flags);
static MprSocketProvider *createMatrixSslProvider(MprCtx ctx);
static MprSocket *createMss(MprCtx ctx, MprSsl *ssl);
static void     disconnectMss(MprSocket *sp);
static int      doHandshake(MprSocket *sp, short cipherSuite);
static int      flushMss(MprSocket *sp);
static MprSsl   *getDefaultMatrixSsl(MprCtx ctx);
static int      innerRead(MprSocket *sp, char *userBuf, int len);
static int      listenMss(MprSocket *sp, cchar *host, int port, MprSocketAcceptProc acceptFn, void *data, int flags);
static int      matrixSslDestructor(MprSsl *ssl);
static int      matrixSslSocketDestructor(MprSslSocket *msp);
static int      readMss(MprSocket *sp, void *buf, int len);
static int      writeMss(MprSocket *sp, void *buf, int len);

/************************************ Code ************************************/

int mprCreateMatrixSslModule(MprCtx ctx, bool lazy)
{
    Mpr                 *mpr;
    MprSocketService    *ss;
    MprSocketProvider   *provider;

    mprAssert(ctx);

    mpr = mprGetMpr(ctx);
    ss = mpr->socketService;

    /*
     *  Install this module as the SSL provider (can only have 1)
     */
    if ((provider = createMatrixSslProvider(mpr)) == 0) {
        return 0;
    }
    mprSetSecureProvider(ss, provider);

    if (matrixSslOpen() < 0) {
        return 0;
    }
    if (!lazy) {
        getDefaultMatrixSsl(ss);
    }
    return 0;
}


static MprSsl *getDefaultMatrixSsl(MprCtx ctx)
{
    Mpr                 *mpr;
    MprSocketService    *ss;
    MprSsl              *ssl;

    mpr = mprGetMpr(ctx);
    ss = mpr->socketService;

    if (ss->secureProvider->defaultSsl) {
        return ss->secureProvider->defaultSsl;
    }
    if ((ssl = mprCreateSsl(ss)) == 0) {
        return 0;
    }
    ss->secureProvider->defaultSsl = ssl;
    return ssl;
}


static MprSocketProvider *createMatrixSslProvider(MprCtx ctx)
{
    Mpr                 *mpr;
    MprSocketProvider   *provider;

    mpr = mprGetMpr(ctx);
    provider = mprAllocObjZeroed(mpr, MprSocketProvider);
    if (provider == 0) {
        return 0;
    }

    provider->name = "MatrixSsl";
    provider->acceptSocket = acceptMss;
    provider->closeSocket = closeMss;
    provider->configureSsl = configureMss;
    provider->connectSocket = connectMss;
    provider->createSocket = createMss;
    provider->disconnectSocket = disconnectMss;
    provider->flushSocket = flushMss;
    provider->listenSocket = listenMss;
    provider->readSocket = readMss;
    provider->writeSocket = writeMss;
    return provider;
}


static int configureMss(MprSsl *ssl)
{
    MprSocketService    *ss;
    char                *password;

    ss = mprGetMpr(ssl)->socketService;

    mprSetDestructor(ssl, (MprDestructor) matrixSslDestructor);

    /*
     *  Read the certificate and the key file for this server. 
     */
    password = NULL;
    mprAssert(ssl->keys == NULL);

    if (matrixSslReadKeys(&ssl->keys, ssl->certFile, ssl->keyFile, password, NULL) < 0) {
        mprError(ssl, "MatrixSSL: Could not read or decode certificate or key file."); 
        return MPR_ERR_CANT_INITIALIZE;
    }

    /*
     *  Select the required protocols. MatrixSSL supports only SSLv3.
     */
    if (ssl->protocols & MPR_HTTP_PROTO_SSLV2) {
        mprError(ssl, "MatrixSSL: SSLv2 unsupported"); 
        return MPR_ERR_CANT_INITIALIZE;
    }
    if (!(ssl->protocols & MPR_HTTP_PROTO_SSLV3)) {
        mprError(ssl, "MatrixSSL: SSLv3 not enabled, unable to continue"); 
        return MPR_ERR_CANT_INITIALIZE;
    }
    if (ssl->protocols & MPR_HTTP_PROTO_TLSV1) {
        mprLog(ssl, 2, "MatrixSSL: Warning, TLSv1 not supported. Using SSLv3 only.");
    }
    return 0;
}


static int matrixSslDestructor(MprSsl *ssl)
{
    if (ssl->keys) {
        matrixSslFreeKeys(ssl->keys);
    }
    return 0;
}


/*
 *  Create a new Matrix socket
 */
static MprSocket *createMss(MprCtx ctx, MprSsl *ssl)
{
    Mpr                 *mpr;
    MprSocketService    *ss;
    MprSocket           *sp;
    MprSslSocket        *msp;
    
    if (ssl == MPR_SECURE_CLIENT) {
        ssl = 0;
    }

    /*
     *  First get a standard socket
     */
    mpr = mprGetMpr(ctx);
    ss = mpr->socketService;
    sp = ss->standardProvider->createSocket(mpr, ssl);
    if (sp == 0) {
        return 0;
    }
    lock(sp);
    sp->provider = ss->secureProvider;

    msp = (MprSslSocket*) mprAllocObjWithDestructorZeroed(sp, MprSslSocket, matrixSslSocketDestructor);
    if (msp == 0) {
        mprFree(sp);
        return 0;
    }
    sp->sslSocket = msp;
    sp->ssl = ssl;
    msp->sock = sp;

    if (ssl) {
        msp->ssl = ssl;
    }
    unlock(sp);
    return sp;
}


/*
 *  Called on mprFree
 */
static int matrixSslSocketDestructor(MprSslSocket *msp)
{
    if (msp->ssl) {
        mprFree(msp->insock.buf);
        mprFree(msp->outsock.buf);
        if (msp->inbuf.buf) {
            mprFree(msp->inbuf.buf);
        }
        matrixSslDeleteSession(msp->mssl);
    }
    return 0;
}


/*
 *  Close a slock
 */
static void closeMss(MprSocket *sp, bool gracefully)
{
    MprSslSocket  *msp;

    mprAssert(sp);

    lock(sp);
    msp = sp->sslSocket;
    mprAssert(msp);

    if (!(sp->flags & MPR_SOCKET_EOF) && msp->ssl && msp->outsock.buf) {
        /*
         *  Flush data. Append a closure alert to any buffered output data, and try to send it.
         *  Don't bother retrying or blocking, we're just closing anyway.
         */
        matrixSslEncodeClosureAlert(msp->mssl, &msp->outsock);
        if (msp->outsock.start < msp->outsock.end) {
            sp->service->standardProvider->writeSocket(sp, msp->outsock.start, msp->outsock.end - msp->outsock.start);
        }
    }
    sp->service->standardProvider->closeSocket(sp, gracefully);
    unlock(sp);
}


static int listenMss(MprSocket *sp, cchar *host, int port, MprSocketAcceptProc acceptFn, void *data, int flags)
{
    return sp->service->standardProvider->listenSocket(sp, host, port, acceptFn, data, flags);
}


/*
 *  Called to accept an incoming connection request
 */
static MprSocket *acceptMss(MprSocket *listen, bool invokeCallback)
{
    MprSocket       *sp;
    MprSslSocket    *msp;

    /*
     *  Do the standard accept stuff
     */
    sp = listen->service->standardProvider->acceptSocket(listen, 0);
    if (sp == 0) {
        return 0;
    }

    lock(sp);
    msp = sp->sslSocket;
    mprAssert(msp);
    mprAssert(msp->ssl);

    /* 
     *  Associate a new ssl session with this socket.  The session represents the state of the ssl protocol over this socket. 
     *  Session caching is handled automatically by this api.
     */
    if (matrixSslNewSession(&msp->mssl, msp->ssl->keys, NULL, SSL_FLAGS_SERVER) < 0) {
        unlock(sp);
        return 0;
    }

    /* 
     *  MatrixSSL doesn't provide buffers for data internally. Define them here to support buffered reading and writing 
     *  for non-blocking sockets. 
     */
    msp->insock.size = MPR_SSL_BUFSIZE;
    msp->insock.start = (uchar*) mprAlloc(msp, msp->insock.size);
    msp->insock.end = msp->insock.buf = msp->insock.start;

    msp->outsock.size = MPR_SSL_BUFSIZE;
    msp->outsock.start = (uchar*) mprAlloc(msp, msp->outsock.size);
    msp->outsock.end = msp->outsock.buf = msp->outsock.start;

    msp->inbuf.size = 0;
    msp->inbuf.start = msp->inbuf.end = msp->inbuf.buf = 0;
    msp->outBufferCount = 0;
    unlock(sp);

    /*
     *  Call the user accept callback. We do not remember the socket handle, it is up to the callback to manage it 
     *  from here on. The callback can delete the socket.
     */
    if (invokeCallback) {
        if (sp->acceptCallback) {
            if ((sp->acceptCallback)(sp, sp->acceptData, sp->clientIpAddr, sp->port) == 0) {
                return 0;
            }
        } else {
            mprFree(sp);
            return 0;
        }
    }
    return sp;
}


/*
 *  Validate the certificate
 */
static int certValidator(sslCertInfo_t *cert, void *arg)
{
    sslCertInfo_t   *next;
    
    /*
     *    Make sure we are checking the last cert in the chain
     */
    next = cert;
    while (next->next != NULL) {
        next = next->next;
    }
    
    /*
     *  Flag a non-authenticated server correctly. Call matrixSslGetAnonStatus later to 
     *  see the status of this connection.
     */
    if (next->verified != 1) {
        return SSL_ALLOW_ANON_CONNECTION;
    }
    return next->verified;
}


/*
 *  Connect as a client
 */
static int connectMss(MprSocket *sp, cchar *host, int port, int flags)
{
    MprSocketService    *ss;
    MprSslSocket        *msp;
    MprSsl              *ssl;
    
    lock(sp);
    ss = sp->service;

    if (sp->service->standardProvider->connectSocket(sp, host, port, flags) < 0) {
        unlock(sp);
        return MPR_ERR_CANT_CONNECT;
    }

    msp = sp->sslSocket;
    mprAssert(msp);

    if (ss->secureProvider->defaultSsl == 0) {
        if ((ssl = getDefaultMatrixSsl(ss)) == 0) {
            unlock(sp);
            return MPR_ERR_CANT_INITIALIZE;
        }
    } else {
        ssl = ss->secureProvider->defaultSsl;
    }
    msp->ssl = ssl;

    if (matrixSslNewSession(&msp->mssl, ssl->keys, NULL, /* SSL_FLAGS_CLIENT_AUTH */ 0) < 0) {
        unlock(sp);
        return -1;
    }
    
    /*
     *  Configure the certificate validator and do the SSL handshake
     */
    matrixSslSetCertValidator(msp->mssl, certValidator, NULL);
    
    if (doHandshake(sp, 0) < 0) {
        unlock(sp);
        return -1;
    }
    unlock(sp);
    return 0;
}


static void disconnectMss(MprSocket *sp)
{
    sp->service->standardProvider->disconnectSocket(sp);
}


static int blockingWrite(MprSocket *sp, sslBuf_t *out)
{
    MprSocketProvider   *standard;
    uchar               *s;
    int                 bytes;

    standard = sp->service->standardProvider;
    
    s = out->start;
    while (out->start < out->end) {
        bytes = standard->writeSocket(sp, out->start, (int) (out->end - out->start));
        if (bytes < 0) {
            return -1;
            
        } else if (bytes == 0) {
            mprSleep(sp, 10);
        }
        out->start += bytes;
    }
    return (int) (out->start - s);
}


/*
 *  Construct the initial HELLO message to send to the server and initiate
 *  the SSL handshake. Can be used in the re-handshake scenario as well.
 */
static int doHandshake(MprSocket *sp, short cipherSuite)
{
    MprSslSocket    *msp;
    char            buf[MPR_SSL_BUFSIZE];
    int             bytes, rc;

    msp = sp->sslSocket;

    /*
     *  MatrixSSL doesn't provide buffers for data internally.  Define them here to support buffered reading 
     *  and writing for non-blocking sockets. Although it causes quite a bit more work, we support dynamically growing
     *  the buffers as needed.
     */
    msp->insock.size = MPR_SSL_BUFSIZE;
    msp->insock.start = msp->insock.end = msp->insock.buf = mprAlloc(msp, msp->insock.size);
    msp->outsock.size = MPR_SSL_BUFSIZE;
    msp->outsock.start = msp->outsock.end = msp->outsock.buf = mprAlloc(msp, msp->outsock.size);
    msp->inbuf.size = 0;
    msp->inbuf.start = msp->inbuf.end = msp->inbuf.buf = NULL;

    bytes = matrixSslEncodeClientHello(msp->mssl, &msp->outsock, cipherSuite);
    if (bytes < 0) {
        mprAssert(bytes < 0);
        goto error;
    }

    /*
     *  Send the hello with a blocking write
     */
    if (blockingWrite(sp, &msp->outsock) < 0) {
        mprError(msp, "MatrixSSL: Error in socketWrite");
        goto error;
    }
    msp->outsock.start = msp->outsock.end = msp->outsock.buf;

    /*
     *  Call sslRead to work through the handshake. Not actually expecting data back, so the finished case 
     *  is simply when the handshake is complete.
     */
readMore:
    rc = innerRead(sp, buf, sizeof(buf));
    
    /*
     *  Reading handshake records should always return 0 bytes, we aren't expecting any data yet.
     */
    if (rc == 0) {
        if (mprIsSocketEof(sp)) {
            goto error;
        }
#if KEEP
        if (status == SSLSOCKET_EOF || status == SSLSOCKET_CLOSE_NOTIFY) {
            goto error;
        }
#endif
        if (matrixSslHandshakeIsComplete(msp->mssl) == 0) {
            goto readMore;
        }

    } else if (rc > 0) {
        mprError(msp, "MatrixSSL: sslRead got %d data in sslDoHandshake %s", rc, buf);
        goto readMore;

    } else {
        mprError(msp, "MatrixSSL: sslRead error in sslDoHandhake");
        goto error;
    }
    return 0;

error:
    return MPR_ERR_CANT_INITIALIZE;
}


/*
 *  Determine if there is buffered data available
 */
static bool isBufferedData(MprSslSocket *msp)
{
    if (msp->ssl == NULL) {
        return 0;
    }
    if (msp->inbuf.buf && msp->inbuf.start < msp->inbuf.end) {
        return 1;
    }
    if (msp->insock.start < msp->insock.end) {
        return 1;
    }
    return 0;
}


static int innerRead(MprSocket *sp, char *userBuf, int len)
{
    MprSslSocket  *msp;
    MprSocketProvider   *standard;
    sslBuf_t            *inbuf;     /* Cached decoded plain text */
    sslBuf_t            *insock;    /* Cached ciphertext to socket */
    uchar               *buf, error, alertLevel, alertDescription;
    int                 bytes, rc, space, performRead, remaining;

    msp = (MprSslSocket*) sp->sslSocket;
    buf = (uchar*) userBuf;

    inbuf = &msp->inbuf;
    insock = &msp->insock;
    standard = sp->service->standardProvider;

    /*
     *  If inbuf is valid, then we have previously decoded data that must be returned, return as much as possible.  
     *  Once all buffered data is returned, free the inbuf.
     */
    if (inbuf->buf) {
        if (inbuf->start < inbuf->end) {
            remaining = (int) (inbuf->end - inbuf->start);
            bytes = min(len, remaining);
            memcpy(buf, inbuf->start, bytes);
            inbuf->start += bytes;
            return bytes;
        }

        mprFree(inbuf->buf);
        inbuf->buf = NULL;
    }

    /*
     *  Pack the buffered socket data (if any) so that start is at zero.
     */
    if (insock->buf < insock->start) {
        if (insock->start == insock->end) {
            insock->start = insock->end = insock->buf;
        } else {
            memmove(insock->buf, insock->start, insock->end - insock->start);
            insock->end -= (insock->start - insock->buf);
            insock->start = insock->buf;
        }
    }

    performRead = 0;

    /*
     *  If we have data still in the buffer, we must process if we can without another read (incase there 
     *  is no more data to read and we block).
     */
    if (insock->end == insock->start) {
readMore:
        //
        //  Read up to as many bytes as there are remaining in the buffer.
        //
        if ((insock->end - insock->start) < insock->size) {
            performRead = 1;
            bytes = standard->readSocket(sp, insock->end, (insock->buf + insock->size) - insock->end);
            if (bytes <= 0 && (insock->end == insock->start)) {
                return bytes;
            }
            insock->end += bytes;
        }
    }

    /*
     *  Define a temporary sslBuf
     */
    inbuf->start = inbuf->end = inbuf->buf = (uchar*) mprAlloc(msp, len);
    inbuf->size = len;

decodeMore:
    /*
     *  Decode the data we just read from the socket
     */
    error = 0;
    alertLevel = 0;
    alertDescription = 0;

    rc = matrixSslDecode(msp->mssl, insock, inbuf, &error, &alertLevel, &alertDescription);
    switch (rc) {

    /*
     *  Successfully decoded a record that did not return data or require a response.
     */
    case SSL_SUCCESS:
        if (insock->end > insock->start) {
            goto decodeMore;
        }
        return 0;

    /*
     *  Successfully decoded an application data record, and placed in tmp buf
     */
    case SSL_PROCESS_DATA:
        //
        //  Copy as much as we can from the temp buffer into the caller's buffer
        //  and leave the remainder in inbuf until the next call to read
        //
        space = (inbuf->end - inbuf->start);
        len = min(space, len);
        memcpy(buf, inbuf->start, len);
        inbuf->start += len;
        return len;

    /*
     *  We've decoded a record that requires a response into tmp If there is no data to be flushed in the out 
     *  buffer, we can write out the contents of the tmp buffer.  Otherwise, we need to append the data 
     *  to the outgoing data buffer and flush it out.
     */
    case SSL_SEND_RESPONSE:
        bytes = standard->writeSocket(sp, inbuf->start, inbuf->end - inbuf->start);
        inbuf->start += bytes;
        if (inbuf->start < inbuf->end) {
            mprSetSocketBlockingMode(sp, 1);
            while (inbuf->start < inbuf->end) {
                bytes = standard->writeSocket(sp, inbuf->start, inbuf->end - inbuf->start);
                if (bytes < 0) {
                    goto readError;
                }
                inbuf->start += bytes;
            }
            mprSetSocketBlockingMode(sp, 0);
        }
        inbuf->start = inbuf->end = inbuf->buf;
        if (insock->end > insock->start) {
            goto decodeMore;
        }
        return 0;

    /*
     *  There was an error decoding the data, or encoding the out buffer. There may be a response data in the out 
     *  buffer, so try to send. We try a single hail-mary send of the data, and then close the socket. Since we're 
     *  closing on error, we don't worry too much about a clean flush.
     */
    case SSL_ERROR:
        mprLog(sp, 4, "MatrixSSL: Closing on protocol error %d", error);
        if (inbuf->start < inbuf->end) {
            mprSetSocketBlockingMode(sp, 0);
            bytes = standard->writeSocket(sp, inbuf->start, inbuf->end - inbuf->start);
        }
        goto readError;

    /*
     *  We've decoded an alert.  The level and description passed into matrixSslDecode are filled in with the specifics.
     */
    case SSL_ALERT:
        if (alertDescription == SSL_ALERT_CLOSE_NOTIFY) {
            goto readZero;
        }
        mprLog(sp, 4, "MatrixSSL: Closing on client alert %d: %d", alertLevel, alertDescription);
        goto readError;

    /*
     *  We have a partial record, we need to read more data off the socket. If we have a completely full insock buffer,
     *  we'll need to grow it here so that we CAN read more data when called the next time.
     */
    case SSL_PARTIAL:
        if (insock->start == insock->buf && insock->end == (insock->buf + insock->size)) {
            if (insock->size > SSL_MAX_BUF_SIZE) {
                goto readError;
            }
            insock->size *= 2;
            insock->start = insock->buf = (uchar*) mprRealloc(msp, insock->buf, insock->size);
            insock->end = insock->buf + (insock->size / 2);
        }
        if (!performRead) {
            performRead = 1;
            mprFree(inbuf->buf);
            inbuf->buf = 0;
            goto readMore;
        }
        goto readZero;

    /*
     *  The out buffer is too small to fit the decoded or response data. Increase the size of the buffer and 
     *  call decode again.
     */
    case SSL_FULL:
        mprAssert(inbuf->start == inbuf->end);
        inbuf->size *= 2;
        if (inbuf->buf != buf) {
            mprFree(inbuf->buf);
            inbuf->buf = 0;
        }
        inbuf->start = (uchar*) mprAlloc(msp, inbuf->size);
        inbuf->end = inbuf->buf = inbuf->start;
        goto decodeMore;
    }

readZero:
    return 0;

readError:
    sp->flags |= MPR_SOCKET_EOF;
    return -1;
}


static int readMss(MprSocket *sp, void *buf, int len)
{
    MprSslSocket  *msp;
    int           bytes;

    lock(sp);
    msp = (MprSslSocket*) sp->sslSocket;

    if (msp->ssl == NULL || len <= 0) {
        unlock(sp);
        return -1;
    }

    bytes = innerRead(sp, buf, len);

    /*
     *  If there is more data buffered locally here, then ensure the select handler will recall us again even 
     *  if there is no more IO events
     */
    if (isBufferedData(msp)) {
        sp->flags |= MPR_SOCKET_PENDING;
        if (sp->handler) {
            mprRecallWaitHandler(sp->handler);
        }
    }
    unlock(sp);
    return bytes;
}


/*
 *  Write data. Return the number of bytes written or -1 on errors.
 *
 *  Encode caller's data buffer into an SSL record and write to socket. The encoded data will always be 
 *  bigger than the incoming data because of the record header (5 bytes) and MAC (16 bytes MD5 / 20 bytes SHA1)
 *  This would be fine if we were using blocking sockets, but non-blocking presents an interesting problem.  Example:
 *
 *      A 100 byte input record is encoded to a 125 byte SSL record
 *      We can send 124 bytes without blocking, leaving one buffered byte
 *      We can't return 124 to the caller because it's more than they requested
 *      We can't return 100 to the caller because they would assume all data
 *      has been written, and we wouldn't get re-called to send the last byte
 *
 *  We handle the above case by returning 0 to the caller if the entire encoded record could not be sent. Returning 
 *  0 will prompt us to select this socket for write events, and we'll be called again when the socket is writable.  
 *  We'll use this mechanism to flush the remaining encoded data, ignoring the bytes sent in, as they have already 
 *  been encoded.  When it is completely flushed, we return the originally requested length, and resume normal 
 *  processing.
 */
static int writeMss(MprSocket *sp, void *buf, int len)
{
    MprSslSocket    *msp;
    sslBuf_t        *outsock;
    int             rc;

    lock(sp);

    msp = (MprSslSocket*) sp->sslSocket;
    outsock = &msp->outsock;

    if (len > SSL_MAX_PLAINTEXT_LEN) {
        len = SSL_MAX_PLAINTEXT_LEN;
    }

    /*
     *  Pack the buffered socket data (if any) so that start is at zero.
     */
    if (outsock->buf < outsock->start) {
        if (outsock->start == outsock->end) {
            outsock->start = outsock->end = outsock->buf;
        } else {
            memmove(outsock->buf, outsock->start, outsock->end - outsock->start);
            outsock->end -= (outsock->start - outsock->buf);
            outsock->start = outsock->buf;
        }
    }

    /*
     *  If there is buffered output data, the caller must be trying to send the same amount of data as last time.  
     *  We don't support sending additional data until the original buffered request has been completely sent.
     */
    if (msp->outBufferCount > 0 && len != msp->outBufferCount) {
        mprAssert(len != msp->outBufferCount);
        unlock(sp);
        return -1;
    }
    
    /*
     *  If we don't have buffered data, encode the caller's data
     */
    if (msp->outBufferCount == 0) {
retryEncode:
        rc = matrixSslEncode(msp->mssl, (uchar*) buf, len, outsock);
        switch (rc) {
        case SSL_ERROR:
            unlock(sp);
            return -1;

        case SSL_FULL:
            if (outsock->size > SSL_MAX_BUF_SIZE) {
                unlock(sp);
                return -1;
            }
            outsock->size *= 2;
            outsock->buf = (uchar*) mprRealloc(msp, outsock->buf, outsock->size);
            outsock->end = outsock->buf + (outsock->end - outsock->start);
            outsock->start = outsock->buf;
            goto retryEncode;
        }
    }

    /*
     *  We've got data to send.  Try to write it all out.
     */
    rc = sp->service->standardProvider->writeSocket(sp, outsock->start, outsock->end - outsock->start);
    if (rc <= 0) {
        unlock(sp);
        return rc;
    }
    outsock->start += rc;

    /*
     *  If we wrote it all return the length, otherwise remember the number of bytes passed in, and return 0 
     *  to be called again later.
     */
    if (outsock->start == outsock->end) {
        msp->outBufferCount = 0;
        unlock(sp);
        return len;
    }
    msp->outBufferCount = len;
    unlock(sp);
    return 0;
}


static int flushMss(MprSocket *sp)
{
    MprSslSocket  *msp;

    msp = (MprSslSocket*) sp->sslSocket;

    if (msp->outsock.start < msp->outsock.end) {
        return sp->service->standardProvider->writeSocket(sp, msp->outsock.start, msp->outsock.end - msp->outsock.start);
    }
    return 0;
}

#else
int mprCreateMatrixSslModule(MprCtx ctx, bool lazy) { return -1; }
void mprMatrixSslModuleDummy() {}
#endif /* BLD_FEATURE_MATRIXSSL */

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
