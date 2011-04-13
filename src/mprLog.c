/**
 *  mprLog.c - Multithreaded Portable Runtime (MPR) Logging and error reporting.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/****************************** Forward Declarations **************************/

static void defaultLogHandler(MprCtx ctx, int flags, int level, cchar *msg);
static void logOutput(MprCtx ctx, int flags, int level, cchar *msg);

/************************************ Code ************************************/
/*
 *  Put first in file so it is easy to locate in a debugger
 */
void mprBreakpoint()
{
#if BLD_DEBUG && DEBUG_IDE
    #if BLD_HOST_CPU_ARCH == MPR_CPU_IX86 || BLD_HOST_CPU_ARCH == MPR_CPU_IX64
        #if WINCE
            /* Do nothing */
        #elif BLD_WIN_LIKE
            __asm { int 3 };
        #else
            asm("int $03");
            /*  __asm__ __volatile__ ("int $03"); */
        #endif
    #endif
#endif
}


void mprLog(MprCtx ctx, int level, cchar *fmt, ...)
{
    va_list     args;
    char        *buf;

    mprAssert(ctx);

    if (level > mprGetLogLevel(ctx)) {
        return;
    }
    va_start(args, fmt);
    buf = mprVasprintf(ctx, -1, fmt, args);
    va_end(args);

    logOutput(ctx, MPR_LOG_SRC, level, buf);
    mprFree(buf);
}


/*
 *  Do raw output
 */
void mprRawLog(MprCtx ctx, int level, cchar *fmt, ...)
{
    va_list     args;
    char        *buf;

    if (level > mprGetLogLevel(ctx)) {
        return;
    }
    va_start(args, fmt);
    buf = mprVasprintf(ctx, -1, fmt, args);
    va_end(args);
    
    logOutput(ctx, MPR_RAW, 0, buf);
    mprFree(buf);
}


/*
 *  Handle an error
 */
void mprError(MprCtx ctx, cchar *fmt, ...)
{
    va_list     args;
    char        *buf;

    va_start(args, fmt);
    buf = mprVasprintf(ctx, -1, fmt, args);
    va_end(args);
    
    logOutput(ctx, MPR_ERROR_MSG | MPR_ERROR_SRC, 0, buf);

    mprFree(buf);
    mprBreakpoint();
}


/*
 *  Handle a memory allocation error
 */
void mprMemoryError(MprCtx ctx, cchar *fmt, ...)
{
    va_list     args;
    char        *buf;

    if (fmt == 0) {
        logOutput(ctx, MPR_ERROR_MSG | MPR_ERROR_SRC, 0, "Memory allocation error");
    } else {
        va_start(args, fmt);
        buf = mprVasprintf(ctx, -1, fmt, args);
        va_end(args);
        logOutput(ctx, MPR_ERROR_MSG | MPR_ERROR_SRC, 0, buf);
        mprFree(buf);
    }
}


/*
 *  Handle an error that should be displayed to the user
 */
void mprUserError(MprCtx ctx, cchar *fmt, ...)
{
    va_list     args;
    char        *buf;

    va_start(args, fmt);
    buf = mprVasprintf(ctx, -1, fmt, args);
    va_end(args);
    
    logOutput(ctx, MPR_USER_MSG | MPR_ERROR_SRC, 0, buf);
    mprFree(buf);
}


/*
 *  Handle a fatal error. Forcibly shutdown the application.
 */
void mprFatalError(MprCtx ctx, cchar *fmt, ...)
{
    va_list     args;
    char        *buf;

    va_start(args, fmt);
    buf = mprVasprintf(ctx, -1, fmt, args);
    va_end(args);
    
    logOutput(ctx, MPR_USER_MSG | MPR_FATAL_SRC, 0, buf);
    mprFree(buf);

#if BREW
    mprSignalExit(ctx);
#else
    exit(2);
#endif
}


/*
 *  Handle an error without allocating memory.
 */
void mprStaticError(MprCtx ctx, cchar *fmt, ...)
{
    va_list     args;
    char        buf[MPR_MAX_STRING];

    va_start(args, fmt);
    mprVsprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    logOutput(ctx, MPR_ERROR_MSG | MPR_ERROR_SRC, 0, buf);
}


/*
 *  Direct output to the standard error. Does not hook into the logging system and does not allocate memory.
 */
void mprStaticAssert(cchar *loc, cchar *msg)
{
#if BLD_DEBUG
    char    buf[MPR_MAX_STRING];

    mprSprintf(buf, sizeof(buf), "Assertion %s, failed at %s\n", msg, loc);
    
#if BLD_UNIX_LIKE || VXWORKS
    write(2, buf, strlen(buf));
#elif BREW || BLD_WIN_LIKE
    /*
     *  Only time we use printf. We can't get an alloc context so we have to use real print
     */
#if BREW && !BREWSIM
    fprintf(stderr, " MP: %s\n", buf);
#else
    fprintf(stderr, "%s\n", buf);
#endif

#endif
    mprBreakpoint();
#endif
}


int mprGetLogLevel(MprCtx ctx)
{
    Mpr     *mpr;

    /*
     *  Leave the code like this so debuggers can patch logLevel before returning.
     */
    mpr = mprGetMpr(ctx);
    return mpr->logLevel;
}


void mprSetLogLevel(MprCtx ctx, int level)
{
    mprGetMpr(ctx)->logLevel = level;
}


/*
 *  Output a log message to the log handler
 */
static void logOutput(MprCtx ctx, int flags, int level, cchar *msg)
{
    MprLogHandler   handler;

    mprAssert(ctx != 0);
    handler = mprGetMpr(ctx)->logHandler;
    if (handler != 0) {
        (handler)(ctx, flags, level, msg);
        return;
    }
    defaultLogHandler(ctx, flags, level, msg);
}


static void defaultLogHandler(MprCtx ctx, int flags, int level, cchar *msg)
{
    Mpr     *mpr;
    char    *prefix;

    mpr = mprGetMpr(ctx);
    prefix = mpr->name;

    if (msg == 0) {
        return;
    }

    while (*msg == '\n') {
        mprPrintfError(ctx, "\n");
        msg++;
    }

    if (flags & MPR_LOG_SRC) {
#if BREW && !BREWSIM
        mprPrintfError(ctx, "%s\n", msg);
#else
        mprPrintfError(ctx, "%s: %d: %s\n", prefix, level, msg);
#endif

    } else if (flags & MPR_ERROR_SRC) {
        /*
         *  Use static printing to avoid malloc when the messages are small.
         *  This is important for memory allocation errors.
         */
        if (strlen(msg) < (MPR_MAX_STRING - 32)) {
            mprStaticPrintfError(ctx, "%s: Error: %s\n", prefix, msg);
        } else {
            mprPrintfError(ctx, "%s: Error: %s\n", prefix, msg);
        }

    } else if (flags & MPR_FATAL_SRC) {
        mprPrintfError(ctx, "%s: Fatal: %s\n", prefix, msg);

    } else if (flags & MPR_RAW) {
        mprPrintfError(ctx, "%s", msg);

    } else {
        return;
    }
}


#if BLD_WIN_LIKE
/*
 *  Return the raw O/S error code
 */
int mprGetOsError()
{
    int     rc;
    rc = GetLastError();

    /*
     *  Client has closed the pipe
     */
    if (rc == ERROR_NO_DATA) {
        return EPIPE;
    }
    return rc;
}


/*
 *  Return the mapped (portable, Posix) error code
 */
int mprGetError()
{
    int     err;

    err = mprGetOsError();

    switch (err) {
    case ERROR_SUCCESS:
        return 0;
    case ERROR_FILE_NOT_FOUND:
        return ENOENT;
    case ERROR_ACCESS_DENIED:
        return EPERM;
    case ERROR_INVALID_HANDLE:
        return EBADF;
    case ERROR_NOT_ENOUGH_MEMORY:
        return ENOMEM;
    case ERROR_PATH_BUSY:
    case ERROR_BUSY_DRIVE:
    case ERROR_NETWORK_BUSY:
    case ERROR_PIPE_BUSY:
    case ERROR_BUSY:
        return EBUSY;
    case ERROR_FILE_EXISTS:
        return EEXIST;
    case ERROR_BAD_PATHNAME:
    case ERROR_BAD_ARGUMENTS:
        return EINVAL;
    case WSAENOTSOCK:
        return ENOENT;
    case WSAEINTR:
        return EINTR;
    case WSAEBADF:
        return EBADF;
    case WSAEACCES:
        return EACCES;
    case WSAEINPROGRESS:
        return EINPROGRESS;
    case WSAEALREADY:
        return EALREADY;
    case WSAEADDRINUSE:
        return EADDRINUSE;
    case WSAEADDRNOTAVAIL:
        return EADDRNOTAVAIL;
    case WSAENETDOWN:
        return ENETDOWN;
    case WSAENETUNREACH:
        return ENETUNREACH;
    case WSAECONNABORTED:
        return ECONNABORTED;
    case WSAECONNRESET:
        return ECONNRESET;
    case WSAECONNREFUSED:
        return ECONNREFUSED;
    case WSAEWOULDBLOCK:
        return EAGAIN;
    }
    return MPR_ERR;
}
#endif


#if MACOSX
/*
 *  Just for conditional breakpoints when debugging in Xcode
 */
int _cmp(char *s1, char *s2)
{
    return !strcmp(s1, s2);
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
