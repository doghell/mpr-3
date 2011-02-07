/**
 *  mprHttp.c - HTTP client (and Http code support)
 *
 *  The HTTP client supports HTTP/1.1 including all methods (DELELTE, GET, OPTIONS, POST, PUT, TRACE), SSL, keep-alive and
 *  chunked transfers. This module is thread-safe. 
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/**************************** Forward Declarations ****************************/

#if BLD_FEATURE_HTTP_CLIENT
static void cleanup(MprHttp *http);
static void completeRequest(MprHttp *http);
static MprHttpRequest *createRequest(MprHttp *http);
static void badRequest(MprHttp *http, cchar *fmt, ...);
static bool parseChunk(MprHttp *http, MprBuf *buf);
static char *getHttpToken(MprBuf *buf, cchar *delim);
static int httpReadEvent(MprHttp *http);
static int  httpDestructor(MprHttp *http);
static void httpTimer(MprHttpService *hs, MprEvent *event);
static int  parseAuthenticate(MprHttp *http, char *authDetails);
static bool parseFirstLine(MprHttp *http, MprBuf *buf);
static bool parseHeaders(MprHttp *http, MprBuf *buf);
static void processResponse(MprHttp *http, MprBuf *buf, int nbytes);
static int  writeData(MprHttp *http, cchar *buf, int len, int block);

#if BLD_DEBUG
static void traceResponseData(MprCtx ctx, cchar *buf, int size);
#define traceData(ctx, buf, size) traceResponseData(ctx, buf, size);
#else
#define traceData(ctx, buf, size)
#endif

#endif /* BLD_FEATURE_HTTP_CLIENT */
/************************************ Locals **********************************/
#if BLD_FEATURE_HTTP
/**
 *  Standard HTTP error code table. This gets defined regardless of whether the HTTP feature is enabled.
 *  This is because some consumers of the MPR want to use server side HTTP, but not embed the client.
 */
typedef struct MprHttpCode {
    int     code;                           /**< Http error code */
    char    *codeString;                    /**< Code as a string (for hashing) */
    char    *msg;                           /**< Error message */
} MprHttpCode;


MprHttpCode MprHttpCodes[] = {
    { 100, "100", "Continue" },
    { 200, "200", "OK" },
    { 201, "201", "Created" },
    { 202, "202", "Accepted" },
    { 204, "204", "No Content" },
    { 205, "205", "Reset Content" },
    { 206, "206", "Partial Content" },
    { 301, "301", "Moved Permanently" },
    { 302, "302", "Moved Temporarily" },
    { 304, "304", "Not Modified" },
    { 305, "305", "Use Proxy" },
    { 307, "307", "Temporary Redirect" },
    { 400, "400", "Bad Request" },
    { 401, "401", "Unauthorized" },
    { 402, "402", "Payment Required" },
    { 403, "403", "Forbidden" },
    { 404, "404", "Not Found" },
    { 405, "405", "Method Not Allowed" },
    { 406, "406", "Not Acceptable" },
    { 408, "408", "Request Time-out" },
    { 409, "409", "Conflict" },
    { 410, "410", "Length Required" },
    { 411, "411", "Length Required" },
    { 413, "413", "Request Entity Too Large" },
    { 414, "414", "Request-URI Too Large" },
    { 415, "415", "Unsupported Media Type" },
    { 416, "416", "Requested Range Not Satisfiable" },
    { 417, "417", "Expectation Failed" },
    { 500, "500", "Internal Server Error" },
    { 501, "501", "Not Implemented" },
    { 502, "502", "Bad Gateway" },
    { 503, "503", "Service Unavailable" },
    { 504, "504", "Gateway Time-out" },
    { 505, "505", "Http Version Not Supported" },
    { 507, "507", "Insufficient Storage" },

    /*
     *  Proprietary codes (used internally) when connection to client is severed
     */
    { 550, "550", "Comms Error" },
    { 551, "551", "General Client Error" },
    { 0,   0 }
};

/************************************ Code **********************************/

/*
 *  Initialize the http service.
 */
MprHttpService *mprCreateHttpService(MprCtx ctx)
{
    MprHttpService      *hs;
    MprHttpCode         *ep;

    hs = mprAllocObjZeroed(ctx, MprHttpService);
    if (hs == 0) {
        return 0;
    }
    hs->connections = mprCreateList(hs);

    hs->codes = mprCreateHash(hs, 41);
    for (ep = MprHttpCodes; ep->code; ep++) {
        mprAddHash(hs->codes, ep->codeString, ep);
    }
#if BLD_FEATURE_MULTITHREAD
    hs->mutex = mprCreateLock(hs);
#endif
    return hs;
}


cchar *mprGetHttpCodeString(MprCtx ctx, int code)
{
    char            key[8];
    MprHttpCode     *ep;
    
    mprItoa(key, sizeof(key), code, 10);
    ep = (MprHttpCode*) mprLookupHash(mprGetMpr(ctx)->httpService->codes, key);
    if (ep == 0) {
        return "Custom error";
    }
    return ep->msg;
}


int mprStartHttpService(MprHttpService *hs)
{
    return 0;
}


int mprStopHttpService(MprHttpService *hs)
{
    mprFree(hs->timer);
    hs->timer = 0;
    return 0;
}
#endif /* BLD_FEATURE_HTTP */


#if BLD_FEATURE_HTTP_CLIENT
static void startHttpTimer(MprHttpService *hs)
{
    mprLock(hs->mutex);
    if (hs->timer) {
        mprUnlock(hs->mutex);
        return;
    }
    hs->timer = mprCreateTimerEvent(mprGetDispatcher(hs), (MprEventProc) httpTimer, MPR_HTTP_TIMER_PERIOD, 
        MPR_NORMAL_PRIORITY, hs, MPR_EVENT_CONTINUOUS);
    mprUnlock(hs->mutex);
}


/*
 *  Check for any expired http connections. One timer to rule them all.
 */
static void httpTimer(MprHttpService *hs, MprEvent *event)
{
    MprHttp     *http;
    MprTime     now;
    int         next, count;

    mprAssert(hs);
    mprAssert(event);
    
    /*
     *  Locking ensures http connections won't be deleted. Must always lock the list before locking the http object.
     */
    mprLock(hs->mutex);

    now = mprGetTime(hs);
    for (count = 0, next = 0; (http = mprGetNextItem(hs->connections, &next)) != 0; count++) {
        /*
         *  See if more than the timeout period has passed since the last I/O. If so, disconnect and let the event 
         *  mechanism clean up. Add grace period of 5 seconds to allow blocked requests to cleanup first. 
         *
         *  The temp "diff" is a Workaround for a GCC bug when comparing two 64 bit numerics directly.
         */
        int64 diff = now - (http->timestamp + http->timeoutPeriod + 5000);
        if (diff > 0 && !mprGetDebugMode(hs)) {
            mprLog(hs, 4, "Request has timed out, timeout %d", http->timeoutPeriod);
            http->timedout = 1;
            mprDisconnectHttp(http);
        }
    }
    if (count == 0) {
        mprFree(event);
        hs->timer = 0;
    }
    mprUnlock(hs->mutex);
}


static void addHttp(MprHttpService *hs, MprHttp *http)
{
    mprLock(hs->mutex);
    mprAddItem(hs->connections, http);
    if (hs->timer == 0) {
        startHttpTimer(hs);
    }
    mprUnlock(hs->mutex);
}


/*
 *  Create a new http instance which represents a single connection to a remote server. Only one request may be active
 *  on a http instance at a time.
 */
MprHttp *mprCreateHttp(MprCtx ctx)
{
    MprHttpService  *hs;
    MprHttp         *http;

    hs = mprGetMpr(ctx)->httpService;
    mprAssert(hs);

    http = mprAllocObjWithDestructorZeroed(ctx, MprHttp, httpDestructor);
    if (http == 0) {
        return 0;
    }
    http->timestamp = mprGetTime(http);
    http->protocolVersion = 1;
    http->protocol = mprStrdup(http, "HTTP/1.1");
    http->state = MPR_HTTP_STATE_BEGIN;
    http->currentPort = -1;
    http->proxyPort = -1;
    http->followRedirects = 1;
    http->defaultHost = mprStrdup(http, "127.0.0.1");
    http->defaultPort = 80;
    http->service = hs;
    http->timeoutPeriod = MPR_TIMEOUT_HTTP;
    http->retries = MPR_HTTP_RETRIES;
    http->useKeepAlive = 1;
    http->bufsize = MPR_HTTP_BUFSIZE;
    http->bufmax = -1;
    http->request = createRequest(http);
#if BLD_FEATURE_MULTITHREAD
    http->mutex = mprCreateLock(http);
#endif
    addHttp(hs, http);
    mprAssert(http->sock == 0);
    return http;
}


static int httpDestructor(MprHttp *http)
{
    MprHttpService      *hs;

    hs = http->service;

    mprLock(hs->mutex);
    mprRemoveItem(hs->connections, http);
    mprFree(http->sock);
    mprUnlock(hs->mutex);
    return 0;
}


void mprDisconnectHttp(MprHttp *http)
{
    mprAssert(http);

    lock(http);
    if (http->sock) {
        mprDisconnectSocket(http->sock);
    }
    unlock(http);
}


/*
 *  Open a new connection to a remote server
 */
static int openConnection(MprHttp *http, cchar *host, int port, bool secure)
{
    int     rc;

    mprAssert(http);
    mprAssert(http->sock == 0);

    mprLog(http, 3, "Http: Opening socket on: %s:%d", host, port);

    if (secure) {
#if BLD_FEATURE_SSL
        http->sock = mprCreateSocket(http, MPR_SECURE_CLIENT);
#else
        return MPR_ERR_BAD_ARGS;
#endif
    } else {
        http->sock = mprCreateSocket(http, NULL);
    }
    rc = mprOpenClientSocket(http->sock, host, port, 0);
    if (rc < 0) {
        mprFree(http->sock);
        http->sock = 0;
        return MPR_ERR_CANT_OPEN;
    }
    mprFree(http->currentHost);
    http->currentHost = mprStrdup(http, host);
    http->currentPort = port;
    http->keepAlive = http->useKeepAlive;
    return 0;
}


/*
 *  Create a new request instance
 */
static MprHttpRequest *createRequest(MprHttp *http)
{
    MprHttpRequest  *req;

    mprAssert(http->state == MPR_HTTP_STATE_BEGIN);

    req = mprAllocObjZeroed(http, MprHttpRequest);
    if (req == 0) {
        return 0;
    }
    req->http = http;
    req->headers = mprCreateHash(req, -1);
    req->outBuf = mprCreateBuf(req, http->bufsize, http->bufmax);
    req->chunked = -1;
    return req;
}


/*
 *  Called for 1XX responses which are ignored. This resets the response to its original condition.
 */
static void resetResponse(MprHttp *http)
{
    MprHttpResponse *resp;

    resp = http->response;
    resp->code = -1;
    mprFlushBuf(resp->headerBuf);
    mprFlushBuf(resp->dataBuf);
    mprFlushBuf(resp->chunkBuf);
    mprFree(resp->headers);
    resp->headers = mprCreateHash(resp, -1);
}


/*
 *  Reset the request if users stats initializing the http object and a request is underway or complete
 */
static void conditionalReset(MprHttp *http)
{
    mprAssert(http);

    if (MPR_HTTP_STATE_BEGIN < http->state && http->state < MPR_HTTP_STATE_COMPLETE) {
        cleanup(http);
        mprFree(http->sock);
        http->sock = 0;
        http->state = MPR_HTTP_STATE_BEGIN;
    }
}


/*
 *  Cleanup called at the completion of a request to prepare for follow-on requests on the same http object.
 */
static void cleanup(MprHttp *http)
{
    MprHttpRequest  *req;

    req = http->request;
    mprAssert(req);

    mprFree(req->headers);
    req->headers = mprCreateHash(req, -1);

    if (req->bodyData != req->formData) {
        mprFree(req->bodyData);
    }
    mprFree(req->formData);
    req->formData = 0;
    req->formLen = 0;
    req->bodyData = 0;
    req->bodyLen = 0;
    req->flags = 0;
    req->chunked = -1;
}


/*
 *  Create a new response object. 
 */
static MprHttpResponse *createResponse(MprHttp *http)
{
    MprHttpResponse *resp;

    resp = mprAllocObjZeroed(http, MprHttpResponse);
    if (resp == 0) {
        return 0;
    }
    resp->headers = mprCreateHash(resp, -1);
    resp->http = http;
    resp->code = -1;
    resp->headerBuf = mprCreateBuf(resp, http->bufsize, http->bufmax);
    resp->dataBuf = mprCreateBuf(resp, http->bufsize, http->bufmax);
    resp->chunkBuf = mprCreateBuf(resp, http->bufsize, http->bufmax);
    return resp;
}


/*
 *  Check the response for authentication failures and redirections. Return true if a retry is requried.
 */
bool mprNeedHttpRetry(MprHttp *http, char **url)
{
    MprHttpResponse     *resp;
    MprHttpRequest      *req;

    mprAssert(http->response);
    mprAssert(http->state > MPR_HTTP_STATE_WAIT);

    /*
     *  For sync mode requests (no callback), handle authorization and redirections inline
     */
    resp = http->response;
    req = http->request;
    *url = 0;

    if (http->state < MPR_HTTP_STATE_WAIT) {
        return 0;
    }
    if (resp->code == MPR_HTTP_CODE_UNAUTHORIZED) {
        if (http->user == 0) {
            http->error = mprStrdup(http, "Authentication required");
        } else if (http->request->sentCredentials) {
            http->error = mprStrdup(http, "Authentication failed");
        } else {
            return 1;
        }
    } else if (MPR_HTTP_CODE_MOVED_PERMANENTLY <= resp->code && resp->code <= MPR_HTTP_CODE_MOVED_TEMPORARILY && 
            http->followRedirects) {
        *url = resp->location;
        return 1;
    }
    return 0;
}


/*
 *  Start a HTTP request. Do not block.
 */
int mprStartHttpRequest(MprHttp *http, cchar *method, cchar *requestUrl)
{
    MprHttpRequest      *req;
    MprHttpResponse     *resp;
    MprUri              *url;
    MprBuf              *outBuf;
    MprHashTable        *headers;
    MprHash             *header;
    char                *host;
    int                 port, len, rc, written;

    mprAssert(http);
    mprAssert(method && *method);
    mprAssert(requestUrl && *requestUrl);

    mprLog(http, 4, "Http: request: %s %s", method, requestUrl);

    rc = 0;
    req = http->request;
    resp = http->response;
    conditionalReset(http);

    /*
     *  Prepare for a new request
     */
    http->timestamp = mprGetTime(req);
    mprFree(http->error);
    http->error = 0;

    outBuf = req->outBuf;
    req->sentCredentials = 0;

    mprFree(req->method);
    method = req->method = mprStrdup(req, method);
    mprStrUpper(req->method);

    mprFree(req->uri);
    url = req->uri = mprParseUri(req, requestUrl);

    mprFree(http->response);
    http->response = createResponse(http);

    if (req->formData) {
        req->bodyData = req->formData;
        req->bodyLen = req->formLen;
    }
    if (*requestUrl == '/') {
        host = (http->proxyHost) ? http->proxyHost : http->defaultHost;
        port = (http->proxyHost) ? http->proxyPort : http->defaultPort;

    } else {
        host = (http->proxyHost) ? http->proxyHost : url->host;
        port = (http->proxyHost) ? http->proxyPort : url->port;
    }
    if (http->sock) {
        if (port != http->currentPort || strcmp(host, http->currentHost) != 0) {
            /*
             *  This request is for a different host or port. Must close socket.
             */
            mprFree(http->sock);
            http->sock = 0;
        }
    }
    if (http->sock == 0) {
        http->secure = url->secure;
        if (openConnection(http, host, port, url->secure) < 0) {
            badRequest(http, "Can't open socket on %s:%d", host, port);
            return MPR_ERR_CANT_OPEN;
        }
    } else {
        mprLog(http, 4, "Http: reusing keep-alive socket on: %s:%d", host, port);
    }

    /*
     *  Emit the request
     */
    if (http->proxyHost && *http->proxyHost) {
        if (url->query && *url->query) {
            mprPutFmtToBuf(outBuf, "%s http://%s:%d%s?%s %s\r\n", method, http->proxyHost, http->proxyPort, 
                url->url, url->query, http->protocol);
        } else {
            mprPutFmtToBuf(outBuf, "%s http://%s:%d%s %s\r\n", method, http->proxyHost, http->proxyPort, url->url,
                http->protocol);
        }
    } else {
        if (url->query && *url->query) {
            mprPutFmtToBuf(outBuf, "%s %s?%s %s\r\n", method, url->url, url->query, http->protocol);
        } else {
            mprPutFmtToBuf(outBuf, "%s %s %s\r\n", method, url->url, http->protocol);
        }
    }

    if (http->authType && strcmp(http->authType, "basic") == 0) {
        char    abuf[MPR_MAX_STRING], encDetails[MPR_MAX_STRING];
        mprSprintf(abuf, sizeof(abuf), "%s:%s", http->user, http->password);
        mprEncode64(encDetails, sizeof(encDetails), abuf);
        mprPutFmtToBuf(outBuf, "Authorization: basic %s\r\n", encDetails);
        req->sentCredentials = 1;

    } else if (http->authType && strcmp(http->authType, "digest") == 0) {
        char    a1Buf[256], a2Buf[256], digestBuf[256];
        char    *ha1, *ha2, *digest, *qop;

        if (http->service->secret == 0 && mprCreateHttpSecret(http) < 0) {
            mprLog(req, MPR_ERROR, "Http: Can't create secret for digest authentication");
            mprFree(req);
            http->request = 0;
            return MPR_ERR_CANT_INITIALIZE;
        }
        mprFree(http->authCnonce);
        mprCalcDigestNonce(http, &http->authCnonce, http->service->secret, 0, http->authRealm);

        mprSprintf(a1Buf, sizeof(a1Buf), "%s:%s:%s", http->user, http->authRealm, http->password);
        len = strlen(a1Buf);
        ha1 = mprGetMD5Hash(req, a1Buf, len, NULL);

        mprSprintf(a2Buf, sizeof(a2Buf), "%s:%s", method, url->url);
        len = strlen(a2Buf);
        ha2 = mprGetMD5Hash(req, a2Buf, len, NULL);

        qop = (http->authQop) ? http->authQop : (char*) "";

        http->authNc++;
        if (mprStrcmpAnyCase(http->authQop, "auth") == 0) {
            mprSprintf(digestBuf, sizeof(digestBuf), "%s:%s:%08x:%s:%s:%s",
                ha1, http->authNonce, http->authNc, http->authCnonce, http->authQop, ha2);
        } else if (mprStrcmpAnyCase(http->authQop, "auth-int") == 0) {
            mprSprintf(digestBuf, sizeof(digestBuf), "%s:%s:%08x:%s:%s:%s",
                ha1, http->authNonce, http->authNc, http->authCnonce, http->authQop, ha2);
        } else {
            qop = "";
            mprSprintf(digestBuf, sizeof(digestBuf), "%s:%s:%s", ha1, http->authNonce, ha2);
        }
        mprFree(ha1);
        mprFree(ha2);
        digest = mprGetMD5Hash(req, digestBuf, strlen(digestBuf), NULL);

        if (*qop == '\0') {
            mprPutFmtToBuf(outBuf, "Authorization: Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", "
                "uri=\"%s\", response=\"%s\"\r\n",
                http->user, http->authRealm, http->authNonce, url->url, digest);

        } else if (strcmp(qop, "auth") == 0) {
            mprPutFmtToBuf(outBuf, "Authorization: Digest username=\"%s\", realm=\"%s\", domain=\"%s\", "
                "algorithm=\"MD5\", qop=\"%s\", cnonce=\"%s\", nc=\"%08x\", nonce=\"%s\", opaque=\"%s\", "
                "stale=\"FALSE\", uri=\"%s\", response=\"%s\"\r\n",
                http->user, http->authRealm, http->authDomain, http->authQop, http->authCnonce, http->authNc,
                http->authNonce, http->authOpaque, url->url, digest);

        } else if (strcmp(qop, "auth-int") == 0) {
            ;
        }
        mprFree(digest);
        req->sentCredentials = 1;
    }

    mprPutFmtToBuf(outBuf, "Host: %s\r\n", host);
    mprPutFmtToBuf(outBuf, "User-Agent: %s\r\n", MPR_HTTP_NAME);

    if (http->protocolVersion == 1) {
        if (http->keepAlive) {
            mprPutFmtToBuf(outBuf, "Connection: Keep-Alive\r\n");
        } else {
            mprPutFmtToBuf(outBuf, "Connection: close\r\n");
        }
        if (req->bodyLen > 0) {
            mprPutFmtToBuf(outBuf, "Content-Length: %d\r\n", req->bodyLen);
            req->chunked = 0;

        } else if (strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) {
            if (req->chunked != 0) {
                mprSetHttpHeader(http, 1, "Transfer-Encoding", "chunked");
                req->chunked = 1;
            }
        } else {
            req->chunked = 0;
        }

    } else {
        http->keepAlive = 0;
        mprPutFmtToBuf(outBuf, "Connection: close\r\n");
    }

    headers = http->request->headers;
    if (mprGetHashCount(headers) > 0) {
        for (header = 0; (header = mprGetNextHash(headers, header)) != 0; ) {
            mprPutFmtToBuf(outBuf, "%s: %s\r\n", header->key, header->data);
        }
    }

    mprAddNullToBuf(outBuf);
    mprLog(req, 3, "\nHttp: @@@ Request =>\n%s", mprGetBufStart(outBuf));

    if (req->chunked != 1) {
        /* As an optimization, omit the trailing CRLF if chunked. This makes adding chunks easier */
        mprPutStringToBuf(outBuf, "\r\n");
    }

    /*
     *  Write the request as a blocking write
     */
    lock(http);
    len = mprGetBufLength(outBuf);
    while (len > 0) {
        mprSetSocketBlockingMode(http->sock, 1);
        if ((written = mprWriteSocket(http->sock, mprGetBufStart(outBuf), len)) <= 0) {
            badRequest(http, "Can't write request to socket");
            mprSetSocketBlockingMode(http->sock, 0);
            unlock(http);
            return MPR_ERR_CANT_WRITE;
        }
        mprSetSocketBlockingMode(http->sock, 0);
        len -= written;
    }
    mprFlushBuf(outBuf);

    /*
     *  Write any assigned body data. Sometimes callers will invoke mprWriteHttp after this call returns.
     */
    if (req->bodyData && writeData(http, req->bodyData, req->bodyLen, 0) < 0) {
        badRequest(http, "Can't write body data");
        unlock(http);
        return MPR_ERR_CANT_WRITE;
    }
    http->state = MPR_HTTP_STATE_WAIT;
    unlock(http);
    return 0;
}


int mprHttpRequest(MprHttp *http, cchar *method, cchar *requestUrl)
{
    int     rc;

    if ((rc = mprStartHttpRequest(http, method, requestUrl)) < 0) {
        return rc;
    }
    if (mprWaitForHttp(http, MPR_HTTP_STATE_COMPLETE, -1) < 0) {
        return MPR_ERR_TIMEOUT;
    }
    mprAssert(http->response);
    return http->response->code;
}


int mprFinalizeHttpWriting(MprHttp *http)
{
    MprHttpRequest  *req;

    lock(http);
    req = http->request;
    if (req->chunked == 1) {
        /*
         *  Emit the chunk trailer to signify the end of body data
         */
        if (writeData(http, "\r\n0\r\n\r\n", 7, 1) < 0) {
            unlock(http);
            return MPR_ERR_CANT_WRITE;
        }
        req->chunked = 0;
    }
    unlock(http);
    return 0;
}


/*
 *  Wait for the Http object to achieve a given state. This will invoke mprFinalizeHttpWriting to write the chunk trailer 
 *  if required. It will also invoke the callback for any read events.
 */
int mprWaitForHttp(MprHttp *http, int state, int timeout)
{
    MprTime     mark;
    int         mask, events;

    if (timeout < 0) {
        timeout = http->timeoutPeriod;
    }
    if (timeout < 0) {
        timeout = MAXINT;
    }
    if (http->state == MPR_HTTP_STATE_BEGIN) {
        return MPR_ERR_BAD_STATE;
    } 
    lock(http);
    if (http->state < state) {
        if (state == MPR_HTTP_STATE_COMPLETE) {
            /*
             *  Incase user forgot to do a write(0), make sure the final chunk trailer has been written.
             */
            if (mprFinalizeHttpWriting(http) < 0) {
                unlock(http);
                return MPR_ERR_CANT_WRITE;
            }
        }
        mark = mprGetTime(http);
        while (http->state < state) {
            mask = MPR_READABLE;
            if (http->callback) {
                mask |= http->callbackMask;
            }
            events = MPR_READABLE;
            if (http->sock) {
                if (!mprIsSocketEof(http->sock) && !mprHasSocketPendingData(http->sock)) {
                    mprSetSocketBlockingMode(http->sock, 1);
                    if (((events = mprWaitForSingleIO(http, http->sock->fd, mask, timeout)) == 0) || 
                            mprGetElapsedTime(http, mark) >= timeout) {
                        if (!mprGetDebugMode(http)) {
                            unlock(http);
                            return MPR_ERR_TIMEOUT;
                        }
                    }
                }
            }
            httpReadEvent(http);
            if (http->callback) {
                (http->callback)(http->callbackArg, events & http->callbackMask);
            }
        }
    }
    unlock(http);
    return 0;
}


/*
 *  Wait for receipt of the response headers from the remote server.
 */
int mprWaitForHttpResponse(MprHttp *http, int timeout)
{
    return mprWaitForHttp(http, MPR_HTTP_STATE_CONTENT, timeout);
}


/*
 *  Read http data. Returns a count of bytes read (this may be less than requested). If a callback is defined, 
 *  this routine will not block. Call mprIsHttpComplete to determine if there is more data to read.
 */
int mprReadHttp(MprHttp *http, char *data, int size)
{
    MprHttpResponse *resp;
    MprBuf          *buf;
    int             nbytes;

    mprAssert(size > 0);

    if (http->state == MPR_HTTP_STATE_BEGIN) {
        return MPR_ERR_BAD_STATE;
    } 
    lock(http);
    resp = http->response;
    buf = resp->dataBuf;
    while (size > mprGetBufLength(buf) && http->state != MPR_HTTP_STATE_COMPLETE) {
        mprCompactBuf(buf);
        nbytes = httpReadEvent(http);
        if (nbytes == 0 && mprGetBufLength(buf) > 0) {
            break;
        }
        if (size <= mprGetBufLength(buf) || http->state == MPR_HTTP_STATE_COMPLETE || http->callback) {
            break;
        }
        /*
         *  Block if no data and no callback
         */
        if (mprGetBufLength(buf) == 0 && !http->callback && http->sock) {
            if (mprWaitForSingleIO(http, http->sock->fd, MPR_READABLE, http->timeoutPeriod) < 0) {
                break;
            }
        }
    }
    unlock(http);
    mprAssert(http->callback || http->state == MPR_HTTP_STATE_COMPLETE || mprGetBufLength(buf) > 0);
    return mprGetBlockFromBuf(buf, data, size);
}


/*
 *  Read a string. This will read all available data. It will block if a callback has not been defined.
 */
char *mprReadHttpString(MprHttp *http)
{
    MprBuf      *buf;
    char        data[MPR_HTTP_BUFSIZE], *result;
    int         count;

    if (http->state == MPR_HTTP_STATE_BEGIN) {
        return 0;
    } 
    buf = mprCreateBuf(http, MPR_HTTP_BUFSIZE, -1);
    do {
        if ((count = mprReadHttp(http, data, sizeof(data))) > 0) {
            mprPutBlockToBuf(buf, data, count);
        }
    } while (count > 0 && !http->callback);

    mprAddNullToBuf(buf);
    result = mprStealBuf(http, buf);
    mprFree(buf);
    return result;
}


/*
 *  Return true if the entire request is complete
 */
bool mprIsHttpComplete(MprHttp *http)
{
    return http->state & MPR_HTTP_STATE_COMPLETE;
}


/*
 *  Determine how much data to read into the input buffer and grow the buffer (for headers) if required.
 */
static int getReadSize(MprHttp *http, MprBuf *buf)
{
    MprHttpResponse     *resp;
    int                 space;

    mprAssert(buf);
    resp = http->response;
    mprAssert(resp);

    space = mprGetBufSpace(buf);
    if (space < MPR_HTTP_BUFSIZE) {
        if (mprGrowBuf(buf, MPR_HTTP_BUFSIZE) < 0) {
            return MPR_ERR_NO_MEMORY;
        }
    }
    space = mprGetBufSpace(buf);
    if (resp && resp->contentRemaining > 0 && http->state >= MPR_HTTP_STATE_CONTENT) {
        space = min(space, resp->contentRemaining);
    }
    mprAssert(0 < space && space <= mprGetBufSize(buf));
    return space;
}


/*
 *  Process to an incoming HTTP response
 */
static int httpReadEvent(MprHttp *http)
{
    MprHttpResponse *resp;
    MprBuf          *buf;
    int             nbytes, len;

    mprAssert(http->sock);
    mprAssert(http->request);
    mprAssert(http->response);

    lock(http);
    resp = http->response;
    http->timestamp = mprGetTime(http);

    if (http->state == MPR_HTTP_STATE_WAIT) {
        buf = resp->headerBuf;
    } else if (resp->flags & MPR_HTTP_RESP_CHUNKED) {
        buf = resp->chunkBuf;
    } else {
        buf = resp->dataBuf;
    }
    mprAssert(buf);
    if ((len = getReadSize(http, buf)) < 0) {
        mprAssert(len > 0);
        unlock(http);
        return 0;
    }
    nbytes = mprReadSocket(http->sock, mprGetBufEnd(buf), len);
    if (nbytes < 0 || (nbytes == 0 && mprIsSocketEof(http->sock))) {
        /* 
         *  Server disconnection
         */
        http->keepAlive = 0;
        if (http->state != MPR_HTTP_STATE_COMPLETE && http->response->contentLength == 0) {
            mprLog(http, 5, "Socket end of file from server, rc %d, errno %d", nbytes, errno);
            if (resp->flags & MPR_HTTP_RESP_CHUNKED) {
                badRequest(http, "Communications error");
            } else {
                http->state = MPR_HTTP_STATE_COMPLETE;
                processResponse(http, buf, nbytes);
            }
        } else {
            badRequest(http, "Communications error");
        }

    } else if (nbytes > 0) {
        mprLog(http, 5, "Read %d bytes from socket, ask for %d", nbytes, len);
        traceData(http, mprGetBufStart(buf), nbytes);
        mprAdjustBufEnd(buf, nbytes);
        processResponse(http, buf, nbytes);
    }
    unlock(http);
    return nbytes;
}


/*
 *  Main HTTP state machine. Process whatever data is available so far.
 */
static void processResponse(MprHttp *http, MprBuf *buf, int nbytes)
{
    MprHttpResponse     *resp;
    MprHttpRequest      *req;
    int                 len;

    resp = http->response;
    req = http->request;

    while (1) {
        switch(http->state) {
        case MPR_HTTP_STATE_WAIT:
            if (!parseFirstLine(http, buf) || !parseHeaders(http, buf)) {
                return;
            }
            resp = http->response;
            if (100 <= resp->code && resp->code < 200) {
                /* Ignore 1XX responses and scan for a new first+headers */
                resetResponse(http);
                break;
            }
            buf = (resp->flags & MPR_HTTP_RESP_CHUNKED) ? resp->chunkBuf : resp->dataBuf;
            if ((len = mprGetBufLength(resp->headerBuf)) > 0) {
                /* Transfer remaining data to the chunk or data buffer */
                mprPutBlockToBuf(buf, mprGetBufStart(resp->headerBuf), len);
            }
            nbytes = mprGetBufLength(buf);
            if (resp->flags & MPR_HTTP_RESP_CHUNKED) {
                http->state = MPR_HTTP_STATE_CHUNK;
            } else {
                http->state = (resp->contentRemaining == 0) ? MPR_HTTP_STATE_COMPLETE: MPR_HTTP_STATE_CONTENT;
            }
            break;

        case MPR_HTTP_STATE_CONTENT:
            if (resp->flags & MPR_HTTP_RESP_CHUNKED) {
                if ((len = mprGetBufLength(buf)) == 0) {
                    return;
                }
                len = min(len, resp->chunkRemaining);
                resp->length += len;
                resp->chunkRemaining -= len;
                mprAssert(resp->chunkRemaining >= 0);
                /* Transfer data chunk to the data buffer */
                mprPutBlockToBuf(resp->dataBuf, mprGetBufStart(buf), len);
                mprAdjustBufStart(buf, len);
                if (resp->chunkRemaining > 0) {
                    return;
                } else {
                    http->state = MPR_HTTP_STATE_CHUNK;
                }
            } else {
                len = nbytes;
                resp->length += len;
                resp->contentRemaining -= len;
                mprAssert(resp->contentRemaining >= 0);
                if (resp->contentRemaining > 0) {
                    return;
                } else {
                    http->state = MPR_HTTP_STATE_COMPLETE;
                }
            }
            break;
                
        case MPR_HTTP_STATE_CHUNK:
            if (!parseChunk(http, buf)) {
                return;
            }
            http->state = (resp->chunkRemaining <= 0) ?  MPR_HTTP_STATE_COMPLETE : MPR_HTTP_STATE_CONTENT;
            break;

        case MPR_HTTP_STATE_COMPLETE:
            completeRequest(http);
            return;
                
        default:
            badRequest(http, "Bad state");
            return;
        }
    }
}


/*
 *  Process the first line of data from the HTTP response
 */
static bool parseFirstLine(MprHttp *http, MprBuf *buf)
{
    MprHttpResponse     *resp;
    char                *start, *end, *code;
    int                 len;

    mprAssert(buf);
    mprAssert(http->response);

    resp = http->response;
    start = mprGetBufStart(buf);
    len = mprGetBufLength(buf);
    if (len == 0 || (end = mprStrnstr(start, "\r\n\r\n", len)) == 0) {
        /* Request is currently incomplete, need wait for more data */
        return 0;
    }
#if BLD_DEBUG
    *end = '\0'; mprLog(http, 3, "\nHttp: @@@ Response =>\n%s\n", start); *end = '\r';
#endif
    resp->protocol = getHttpToken(buf, " ");
    if (resp->protocol == 0 || resp->protocol[0] == '\0') {
        badRequest(http, "Bad HTTP response");
        return 0;
    }
    if (strncmp(resp->protocol, "HTTP/1.", 7) != 0) {
        badRequest(http, "Unsupported protocol");
        return 0;
    }
    code = getHttpToken(buf, " ");
    if (code == 0 || *code == '\0') {
        badRequest(http, "Bad HTTP response");
        return 0;
    }
    resp->code = atoi(code);
    resp->message = getHttpToken(buf, "\r\n");
    return 1;
}


/*
 *  Parse the response headers. Only come here when all the headers are resident.
 */
static bool parseHeaders(MprHttp *http, MprBuf *buf)
{
    MprHttpResponse *resp;
    char            *key, *value, *tp;
    int             len;

    resp = http->response;

    while (mprGetBufLength(buf) > 0 && buf->start[0] != '\r') {
        if ((key = getHttpToken(buf, ":")) == 0) {
            badRequest(http, "Bad HTTP header");
            return 0;
        }
        /*
         *  Tokenize the headers insitu. This modifies the data in the input buffer
         */
        value = getHttpToken(buf, "\r\n");
        while (isspace((int) *value)) {
            value++;
        }
        /*
         *  Save each header in the headers hash. Not strduped, these are references into the buffer.
         */
        mprAddHash(resp->headers, mprStrUpper(key), value);

        switch (key[0]) {
        case 'C':
            if (strcmp("CONTENT-LENGTH", key) == 0) {
                resp->contentLength = atoi(value);
                if (resp->contentLength < 0) {
                    resp->contentLength = 0;
                }
                if (mprStrcmpAnyCase(resp->http->request->method, "HEAD") == 0 || (resp->flags & MPR_HTTP_RESP_CHUNKED)) {
                    resp->contentLength = 0;
                    resp->contentRemaining = 0;
                } else {
                    resp->contentRemaining = resp->contentLength;
                }

            } else if (strcmp("CONNECTION", key) == 0) {
                mprStrLower(value);
                if (strcmp(value, "close") == 0) {
                    http->keepAlive = 0;
                    if (resp->contentLength == 0) {
                        resp->contentRemaining = MAXINT;
                    }
                }
            }
            break;
                
        case 'K':
            if (strcmp("KEEP-ALIVE", key) == 0) {
                /*
                 *  Quick compare for "Keep-Alive: timeout=N, max=1"
                 */
                len = (int) strlen(value);
                if (len > 2 && value[len - 1] == '1' && value[len - 2] == '=' && 
                        tolower((int)(value[len - 3])) == 'x') {
                    /*
                     *  IMPORTANT: Deliberately close the connection one request early. This ensures a client-led 
                     *  termination and helps relieve server-side TIME_WAIT conditions.
                     */
                    http->keepAlive = 0;
                }
            }
            break;                
                
        case 'L':
            if (strcmp("LOCATION", key) == 0) {
                resp->location = value;
            }
            break;

        case 'T':
            if (strcmp("TRANSFER-ENCODING", key) == 0) {
                mprStrLower(value);
                if (strcmp(value, "chunked") == 0) {
                    resp->flags |= MPR_HTTP_RESP_CHUNKED;
                    resp->contentLength = 0;
                    resp->contentRemaining = 0;
                }
            }
            break;
        
        case 'W':
            if (strcmp("WWW-AUTHENTICATE", key) == 0) {
                tp = value;
                while (*value && !isspace((int) *value)) {
                    value++;
                }
                *value++ = '\0';
                mprStrLower(tp);
                
                mprFree(http->authType);
                http->authType = mprStrdup(http, tp);
                
                if (parseAuthenticate(http, value) < 0) {
                    badRequest(http, "Bad Authentication header");
                    return 0;
                }
            }
            break;
        }
    }

    /* 
     *  Step over "\r\n", except if chunked: optimization for response chunking to simplify chunk boundary parsing.
     */
    if (!(resp->flags & MPR_HTTP_RESP_CHUNKED)) {
        mprAdjustBufStart(buf, 2);
    }
    return 1;
}


/*
 *  Parse an authentication response
 */
static int parseAuthenticate(MprHttp *http, char *authDetails)
{
    MprHttpResponse *resp;
    char            *value, *tok, *key, *dp, *sp;
    int             seenComma;

    key = (char*) authDetails;
    resp = http->response;

    while (*key) {
        while (*key && isspace((int) *key)) {
            key++;
        }
        tok = key;
        while (*tok && !isspace((int) *tok) && *tok != ',' && *tok != '=') {
            tok++;
        }
        *tok++ = '\0';

        while (isspace((int) *tok)) {
            tok++;
        }
        seenComma = 0;
        if (*tok == '\"') {
            value = ++tok;
            while (*tok != '\"' && *tok != '\0') {
                tok++;
            }
        } else {
            value = tok;
            while (*tok != ',' && *tok != '\0') {
                tok++;
            }
            seenComma++;
        }
        *tok++ = '\0';

        /*
         *  Handle back-quoting
         */
        if (strchr(value, '\\')) {
            for (dp = sp = value; *sp; sp++) {
                if (*sp == '\\') {
                    sp++;
                }
                *dp++ = *sp++;
            }
            *dp = '\0';
        }

        /*
         *  algorithm, domain, nonce, oqaque, realm, qop, stale
         *  We don't strdup any of the values as the headers are persistently saved.
         */
        switch (tolower((int) *key)) {
        case 'a':
            if (mprStrcmpAnyCase(key, "algorithm") == 0) {
                mprFree(resp->authAlgorithm);
                resp->authAlgorithm = value;
                break;
            }
            break;

        case 'd':
            if (mprStrcmpAnyCase(key, "domain") == 0) {
                mprFree(http->authDomain);
                http->authDomain = mprStrdup(http, value);
                break;
            }
            break;

        case 'n':
            if (mprStrcmpAnyCase(key, "nonce") == 0) {
                mprFree(http->authNonce);
                http->authNonce = mprStrdup(http, value);
                resp->http->authNc = 0;
            }
            break;

        case 'o':
            if (mprStrcmpAnyCase(key, "opaque") == 0) {
                mprFree(http->authOpaque);
                http->authOpaque = mprStrdup(http, value);
            }
            break;

        case 'q':
            if (mprStrcmpAnyCase(key, "qop") == 0) {
                mprFree(http->authQop);
                http->authQop = mprStrdup(http, value);
            }
            break;

        case 'r':
            if (mprStrcmpAnyCase(key, "realm") == 0) {
                mprFree(http->authRealm);
                http->authRealm = mprStrdup(http, value);
            }
            break;

        case 's':
            if (mprStrcmpAnyCase(key, "stale") == 0) {
                resp->authStale = mprStrdup(resp, value);
                break;
            }

        default:
            /*  For upward compatibility --  ignore keywords we don't understand */
            ;
        }
        key = tok;
        if (!seenComma) {
            while (*key && *key != ',') {
                key++;
            }
            if (*key) {
                key++;
            }
        }
    }
    if (strcmp(resp->http->authType, "basic") == 0) {
        if (http->authRealm == 0) {
            return MPR_ERR_BAD_ARGS;
        }
        return 0;
    }
    if (http->authRealm == 0 || http->authNonce == 0) {
        return MPR_ERR_BAD_ARGS;
    }
    if (http->authQop) {
        if (http->authDomain == 0 || http->authOpaque == 0 || resp->authAlgorithm == 0 || resp->authStale == 0) {
            return MPR_ERR_BAD_ARGS;
        }
    }
    return 0;
}


/*
 *  Write a block of data data. Uses blocking writes. Callers who need non-blocking writes should use mprWriteSocket
 */
static int writeData(MprHttp *http, cchar *buf, int size, int block)
{
    int     written, rc, nbytes, oldMode;

    http->timestamp = mprGetTime(http);
    block |= http->callback ? 0 : 1;
    oldMode = mprSetSocketBlockingMode(http->sock, block);
    for (written = 0; written < size; ) {
        nbytes = size - written;
        rc = mprWriteSocket(http->sock, (char*) &buf[written], nbytes);
        if (rc < 0) {
            mprSetSocketBlockingMode(http->sock, oldMode);
            return rc;
        }
        written += rc;
        if (rc != nbytes) {
            break;
        }
    }
    mprSetSocketBlockingMode(http->sock, oldMode);
    return written;
}


/*
 *  Define a body to send with the request. This body is sent at the same time as the headers. Use this if you are NOT
 *  using mprWriteHttp. The body parameter can be NULL to just define a content length.
 */
int mprSetHttpBody(MprHttp *http, cchar *body, int len)
{
    MprHttpRequest      *req;

    mprAssert(len >= 0);
    req = http->request;

    conditionalReset(http);
    if (body && len > 0) {
        req->bodyData = mprMemdup(req, body, len);
        if (req->bodyData == 0) {
            return MPR_ERR_NO_MEMORY;
        }
    }
    req->bodyLen = len;
    return 0;
}


/*
 *  Add Form data
 */
int mprAddHttpFormData(MprHttp *http, cchar *body, int len)
{
    MprHttpRequest      *req;

    req = http->request;
    conditionalReset(http);

    req->formData = mprRealloc(req, req->formData, req->formLen + len + 1);
    if (req->formData == 0) {
        return MPR_ERR_NO_MEMORY;
    }
    memcpy(&req->formData[req->formLen], body, len);
    req->formLen += len;
    req->formData[req->formLen] = '\0';
    if (req->formData) {
        mprSetHttpHeader(http, 1, "Content-Type", "application/x-www-form-urlencoded");
    }
    return 0;
}


/*
 *  Add a keyword value pair to the form data
 */
int mprAddHttpFormItem(MprHttp *http, cchar *keyArg, cchar *valueArg)
{
    MprHttpRequest  *req;
    char            *value, *key, *encodedKey, *encodedValue;

    req = http->request;
    conditionalReset(http);

    if (req == 0) {
        return MPR_ERR_BAD_STATE;
    }
    key = (char*) keyArg;
    value = (char*) valueArg;

    if (value == 0) {
        key = mprStrdup(http, key);
        if ((value = strchr(key, '=')) != 0) {
            *value++ = '\0';
        }
    }
    if (key == 0 || value == 0) {
        return MPR_ERR_BAD_ARGS;
    }

    /*
     *  Encode key and value separately
     */
    encodedKey = mprUrlEncode(http, key);
    encodedValue = mprUrlEncode(http, value);
    if (req->formData) {
        req->formData = mprReallocStrcat(req, -1, req->formData, "&", encodedKey, "=", encodedValue, NULL);
    } else {
        req->formData = mprStrcat(req, -1, encodedKey, "=", encodedValue, NULL);
    }
    mprFree(encodedValue);
    if (req->formData == 0) {
        return 0;
    }
    req->formLen = strlen(req->formData);
    return 0;
}


/*
 *  Set the request as being a multipart mime upload. This defines the content type and defines a multipart mime boundary
 */
void mprEnableHttpUpload(MprHttp *http)
{
    conditionalReset(http);
    mprFree(http->boundary);
    http->boundary = mprAsprintf(http, -1, "--BOUNDARY--%Ld", mprGetTime(http));
    mprSetFormattedHttpHeader(http, 1, "Content-Type", "multipart/form-data; boundary=%s", &http->boundary[2]);
}


/*
 *  Returns -1 if chunked transfers are undefined. Returns 0 for disabled, 1 for true
 */
int mprGetHttpChunked(MprHttp *http)
{
    MprHttpRequest  *req;

    req = http->request;
    return req->chunked;
}


/*
 *  Enable or disable chunked transfers. The req->chunked field is initially set to -1 which means undefined. Calling 
 *  this routine will define chunking as explicitly on or off.
 */
int mprSetHttpChunked(MprHttp *http, int enable)
{
    MprHttpRequest  *req;

    req = http->request;
    conditionalReset(http);

    if (enable == 1) {
        req->chunked = 1;
    } else {
        req->chunked = 0;
    }
    return 0;
}


/*
 *  This can write a complete body or just a chunk. If mprSetHttpChunked has been called, then the output will use
 *  transfer chunking.
 */
int mprWriteHttp(MprHttp *http, cchar *buf, int len)
{
    MprHttpRequest  *req;
    char            countBuf[16];
    int             rc;

    mprAssert(http);
    mprAssert(buf);
    mprAssert(len >= 0);

    req = http->request;
    if (len == 0 && buf) {
        len = (int) strlen(buf);
    }
    /*
     *  Use chunk_emitted flag to support non-blocking short writes
     */ 
    if (req->chunked == 1 && !(req->flags & MPR_HTTP_REQ_CHUNK_EMITTED)) {
        if (len == 0) {
            http->callbackMask &= ~MPR_WRITABLE;
            if (mprFinalizeHttpWriting(http) < 0) {
                return MPR_ERR_CANT_WRITE;
            }
            return 0;
        }
        mprSprintf(countBuf, sizeof(countBuf), "\r\n%x\r\n", len);
        if (writeData(http, countBuf, strlen(countBuf), 1) < 0) {
            req->flags |= MPR_HTTP_REQ_CHUNK_EMITTED;
            return MPR_ERR_CANT_WRITE;
        }
        req->flags |= MPR_HTTP_REQ_CHUNK_EMITTED;
    }
    if ((rc = writeData(http, buf, len, 0)) == len) {
        req->flags &= ~MPR_HTTP_REQ_CHUNK_EMITTED;
    }
    return rc;
}


/*
 *  Blocking file copy
 */
static int copyFile(MprHttp *http, cchar *path)
{
    MprFile     *file;
    char        buf[MPR_BUFSIZE];
    int         bytes;

    file = mprOpen(http, path, O_RDONLY | O_BINARY, 0);
    if (file == 0) {
        mprError(http, "Can't open %s", path);
        return MPR_ERR_CANT_OPEN;
    }
    while ((bytes = mprRead(file, buf, sizeof(buf))) > 0) {
        if (mprWriteHttp(http, buf, bytes) != bytes) {
            mprFree(file);
            return MPR_ERR_CANT_WRITE;
        }
    }
    mprFree(file);
    return 0;
}


/*
 *  Write a formatted string. Always block.
 */
static int writeFmt(MprHttp *http, cchar *fmt, ...)
{
    va_list     ap;
    char        *data;
    int         len;

    va_start(ap, fmt);
    data = mprVasprintf(http, -1, fmt, ap);
    va_end(ap);

    len = strlen(data);
    if (mprWriteHttp(http, data, len) != len) {
        return MPR_ERR_CANT_WRITE;
    }
    return 0;
}


/*
 *  Write upload data. This routine blocks. If you need non-blocking ... cut and paste.
 */
int mprWriteHttpUploadData(MprHttp *http, MprList *fileData, MprList *formData)
{
    char        *path, *pair, *key, *value, *name;
    int         next, rc, oldMode;

    oldMode = mprSetSocketBlockingMode(http->sock, 1);
    rc = 0;

    if (formData) {
        for (rc = next = 0; !rc && (pair = mprGetNextItem(formData, &next)) != 0; ) {
            key = mprStrTok(mprStrdup(http, pair), "=", &value);
            rc += writeFmt(http, "%s\r\nContent-Disposition: form-data; name=\"%s\";\r\n", http->boundary, key);
            rc += writeFmt(http, "Content-Type: application/x-www-form-urlencoded\r\n\r\n%s\r\n", value);
        }
    }
    if (fileData) {
        for (rc = next = 0; !rc && (path = mprGetNextItem(fileData, &next)) != 0; ) {
            name = mprGetPathBase(http, path);
            rc += writeFmt(http, "%s\r\nContent-Disposition: form-data; name=\"file%d\"; filename=\"%s\"\r\n", 
                http->boundary, next - 1, name);
            mprFree(name);
            rc += writeFmt(http, "Content-Type: %s\r\n\r\n", mprLookupMimeType(http, path));
            rc += copyFile(http, path);
            rc += writeFmt(http, "\r\n", value);
        }
    }
    rc += writeFmt(http, "%s--\r\n--", http->boundary);
    if (mprFinalizeHttpWriting(http) < 0) {
        mprSetSocketBlockingMode(http->sock, oldMode);
        return MPR_ERR_CANT_WRITE;
    }
    mprSetSocketBlockingMode(http->sock, oldMode);
    return rc;
}


#if BLD_DEBUG
static void traceResponseData(MprCtx ctx, cchar *src, int size)
{
    char    dest[512];
    int     index, i;

    mprRawLog(ctx, 5, "@@@ Response data => \n");

    for (index = 0; index < size; ) { 
        for (i = 0; i < (sizeof(dest) - 1) && index < size; i++) {
            dest[i] = src[index];
            index++;
        }
        dest[i] = '\0';
        mprRawLog(ctx, 5, "%s", dest);
    }
    mprRawLog(ctx, 5, "\n");
}
#endif


int mprGetHttpCode(MprHttp *http)
{
    if (mprWaitForHttpResponse(http, -1) < 0) {
        return 0;
    }
    return http->response->code;
}


cchar *mprGetHttpMessage(MprHttp *http)
{
    if (mprWaitForHttpResponse(http, -1) < 0) {
        return 0;
    }
    return http->response->message;
}


int mprGetHttpContentLength(MprHttp *http)
{
    if (mprWaitForHttpResponse(http, -1) < 0) {
        return 0;
    }
    return http->response->contentLength;
}


cchar *mprGetHttpHeader(MprHttp *http, cchar *key)
{
    cchar   *value;
    char    *upperKey;

    if (mprWaitForHttpResponse(http, -1) < 0) {
        return 0;
    }
    upperKey = mprStrdup(http, key);
    mprStrUpper(upperKey);
    value = mprLookupHash(http->response->headers, upperKey);
    mprFree(upperKey);
    return value;
}


char *mprGetHttpHeaders(MprHttp *http)
{
    MprHttpResponse     *resp;
    MprHash             *hp;
    char                *headers, *key, *cp;
    int                 len;

    if (mprWaitForHttpResponse(http, -1) < 0) {
        return 0;
    }
    resp = http->response;
    headers = 0;
    for (len = 0, hp = mprGetFirstHash(resp->headers); hp; ) {
        headers = mprReallocStrcat(http, -1, headers, hp->key, NULL);
        key = &headers[len];
        for (cp = &key[1]; *cp; cp++) {
            *cp = tolower((int) *cp);
            if (*cp == '-') {
                cp++;
            }
        }
        headers = mprReallocStrcat(http, -1, headers, ": ", hp->data, "\n", NULL);
        len = strlen(headers);
        hp = mprGetNextHash(resp->headers, hp);
    }
    return headers;
}


MprHashTable *mprGetHttpHeadersHash(MprHttp *http)
{
    if (mprWaitForHttpResponse(http, -1) < 0) {
        return 0;
    }
    return http->response->headers;
}


cchar *mprGetHttpError(MprHttp *http)
{
    if (http->error) {
        return http->error;
    } else if (http->state > MPR_HTTP_STATE_WAIT) {
        return mprGetHttpCodeString(http, http->response->code);
    } else {
        return "";
    }
}


void mprSetHttpProxy(MprHttp *http, cchar *host, int port)
{
    conditionalReset(http);
    mprFree(http->proxyHost);
    http->proxyHost = mprStrdup(http, host);
    http->proxyPort = port;
}


void mprSetHttpCallback(MprHttp *http, MprHttpProc fn, void *arg, int mask)
{
    conditionalReset(http);
    http->callback = fn;
    http->callbackArg = arg;
    http->callbackMask = mask;
}


int mprGetHttpState(MprHttp *http)
{
    return http->state;
}


int mprGetHttpFlags(MprHttp *http)
{
    return http->response->flags;
}


void mprSetHttpKeepAlive(MprHttp *http, bool on)
{
    conditionalReset(http);
    http->useKeepAlive = on;
    http->keepAlive = on;
}


void mprSetHttpProtocol(MprHttp *http, cchar *protocol)
{
    conditionalReset(http);
    mprFree(http->protocol);
    http->protocol = mprStrdup(http, protocol);
    if (strcmp(http->protocol, "HTTP/1.0") == 0) {
        http->useKeepAlive = 0;
        http->keepAlive = 0;
        http->protocolVersion = 0;
    }
}


void mprSetHttpRetries(MprHttp *http, int num)
{
    http->retries = num;
}


int mprGetHttpDefaultPort(MprHttp *http)
{
    return http->defaultPort;
}


cchar *mprGetHttpDefaultHost(MprHttp *http)
{
    return http->defaultHost;
}


void mprSetHttpDefaultPort(MprHttp *http, int num)
{
    http->defaultPort = num;
}


void mprSetHttpDefaultHost(MprHttp *http, cchar *host)
{
    mprFree(http->defaultHost);
    http->defaultHost = mprStrdup(http, host);
}


void mprSetHttpContentLength(MprHttp *http, int length)
{
    char    buf[16];

    conditionalReset(http);
    mprItoa(buf, sizeof(buf), (int64) length, 10);
    mprSetHttpHeader(http, 1, "Content-Length", buf);
}


void mprSetHttpCredentials(MprHttp *http, cchar *username, cchar *password)
{
    conditionalReset(http);
    mprResetHttpCredentials(http);
    http->user = mprStrdup(http, username);
    if (password == NULL && strchr(username, ':') != 0) {
        http->user = mprStrTok(http->user, ":", &http->password);
    } else {
        http->password = mprStrdup(http, password);
    }
}


void mprResetHttpCredentials(MprHttp *http)
{
    mprFree(http->user);
    mprFree(http->password);
    mprFree(http->authDomain);
    mprFree(http->authCnonce);
    mprFree(http->authNonce);
    mprFree(http->authOpaque);
    mprFree(http->authRealm);
    mprFree(http->authQop);
    mprFree(http->authType);

    http->user = 0;
    http->password = 0;
    http->authType = 0;
    http->authDomain = 0;
    http->authCnonce = 0;
    http->authNonce = 0;
    http->authOpaque = 0;
    http->authRealm = 0;
    http->authQop = 0;
    http->authType = 0;
}


void mprSetHttpFollowRedirects(MprHttp *http, bool follow)
{
    http->followRedirects = follow;
}


int mprSetHttpHeader(MprHttp *http, bool overwrite, cchar *key, cchar *value)
{
    MprHttpRequest  *req;
    char            *persistKey, *persistValue;

    req = http->request;
    conditionalReset(http);

    persistKey = mprStrdup(req->headers, key);
    persistValue = mprStrdup(req->headers, value);
    if (overwrite) {
        mprAddHash(req->headers, persistKey, persistValue);
    } else {
        mprAddDuplicateHash(req->headers, persistKey, persistValue);
    }
    return 0;
}


int mprSetFormattedHttpHeader(MprHttp *http, bool overwrite, cchar *key, cchar *fmt, ...)
{
    va_list     args;
    char        *value;

    va_start(args, fmt);
    value = mprVasprintf(http, -1, fmt, args);
    va_end(args);
    mprSetHttpHeader(http, overwrite, key, value);
    mprFree(value);
    return 0;
}


int mprSetHttpTimeout(MprHttp *http, int timeout)
{
    int     old;

    conditionalReset(http);

    old = http->timeoutPeriod;
    http->timeoutPeriod = timeout;
    return old;
}


/*
 *  Create a random secret for use in authentication. Create once for the entire http service. Created on demand.
 *  Users can recall as required to update.
 */
int mprCreateHttpSecret(MprCtx ctx)
{
    MprHttpService  *hs;
    char            *hex = "0123456789abcdef";
    char            bytes[MPR_HTTP_MAX_SECRET], ascii[MPR_HTTP_MAX_SECRET * 2 + 1], *ap;
    int             i;

    hs = mprGetMpr(ctx)->httpService;

    if (mprGetRandomBytes(hs, bytes, sizeof(bytes), 0) < 0) {
        mprAssert(0);
        return MPR_ERR_CANT_INITIALIZE;
    }

    ap = ascii;
    for (i = 0; i < (int) sizeof(bytes); i++) {
        *ap++ = hex[bytes[i] >> 4];
        *ap++ = hex[bytes[i] & 0xf];
    }
    *ap = '\0';

    mprFree(hs->secret);
    hs->secret = mprStrdup(hs, ascii);

    return 0;
}


/*
 *  Get the next input token. The conn->input content buffer is advanced to the next token. This routine
 *  always returns a non-zero token. The empty string means the delimiter was not found.
 */
static char *getHttpToken(MprBuf *buf, cchar *delim)
{
    char    *token, *nextToken;
    int     len;

    len = mprGetBufLength(buf);
    if (len == 0) {
        return "";
    }

    token = mprGetBufStart(buf);
    nextToken = mprStrnstr(mprGetBufStart(buf), delim, len);
    if (nextToken) {
        *nextToken = '\0';
        len = (int) strlen(delim);
        nextToken += len;
        buf->start = nextToken;
    } else {
        buf->start = mprGetBufEnd(buf);
    }
    return token;
}


/*
 *  Get the next chunk size. Chunked data format is:
 *      Chunk spec <CRLF>
 *      Data <CRLF>
 *      Chunk spec (size == 0) <CRLF>
 *      <CRLF>
 *
 *  Chunk spec is: "HEX_COUNT; chunk length DECIMAL_COUNT\r\n". The "; chunk length DECIMAL_COUNT is optional.
 *  As an optimization, use "\r\nSIZE ...\r\n" as the delimiter so that the CRLF after data does not special 
 *  consideration. Achive this by parseHeaders reversing the input start by 2.
 *  Return false if the chunk could not be parsed due to lack of data AND the request has not failed.
 */
static bool parseChunk(MprHttp *http, MprBuf *buf)
{
    MprHttpResponse *resp;
    char            *start, *cp;
    int             bad;

    resp = http->response;
    resp->chunkRemaining = 0;

    if (mprGetBufLength(buf) < 5) {
        return 0;
    }
    /*
     *  Validate "\r\nSIZE.*\r\n"
     */
    start = mprGetBufStart(buf);
    bad = (start[0] != '\r' || start[1] != '\n');
    for (cp = &start[2]; cp < (char*) buf->end && *cp != '\n'; cp++) {}
    if (*cp != '\n' && (cp - start) < 80) {
        /* Insufficient data */
        return 0;
    }
    bad += (cp[-1] != '\r' || cp[0] != '\n');
    if (bad) {
        badRequest(http, "Bad chunk specification");
        return 1;
    }
    resp->chunkRemaining = (int) mprAtoi(&start[2], 16);
    if (!isxdigit((int) start[2]) || resp->chunkRemaining < 0) {
        badRequest(http, "Bad chunk specification");
        return 1;
    }
    mprAdjustBufStart(buf, cp - start + 1);
    return 1;
}


/*
 *  Handle a bad request
 */
static void badRequest(MprHttp *http, cchar *fmt, ...)
{
    va_list     args;

    mprAssert(fmt);

    if (http->error == NULL) {
        va_start(args, fmt);
        http->error = mprVasprintf(http, MPR_MAX_STRING, fmt, args);
        va_end(args);
    }
    mprLog(http, 3, "Http: badRequest: %s", http->error);
    http->keepAlive = 0;
    if (http->response) {
        http->response->code = MPR_HTTP_CODE_COMMS_ERROR;
    }
    completeRequest(http);
}


/*
 *  Complete a request. And prepare the http object for a new request.
 */
static void completeRequest(MprHttp *http)
{
    if (http->sock) {
        if (http->keepAlive) {
            mprLog(http, 4, "Http: completeRequest: Attempting keep-alive");
        } else {
            mprCloseSocket(http->sock, 1);
            mprFree(http->sock);
            http->sock = 0;
        }
    }
    http->state = MPR_HTTP_STATE_COMPLETE;
    cleanup(http);
}


#else /* BLD_FEATURE_HTTP_CLIENT */

void __dummy_httpClient() {}
#endif /* BLD_FEATURE_HTTP_CLIENT */

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
