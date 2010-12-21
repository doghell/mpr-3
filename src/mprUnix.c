/**
 *  mprUnix.c - Unix specific adaptions
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

#if BLD_UNIX_LIKE
/*********************************** Code *************************************/

MprOsService *mprCreateOsService(MprCtx ctx)
{
    MprOsService    *os;

    os = mprAllocObj(ctx, MprOsService);
    if (os == 0) {
        return 0;
    }

    umask(022);

    /*
     *  Cleanup the environment. IFS is often a security hole
     */
     putenv("IFS=\t ");
    return os;
}


int mprStartOsService(MprOsService *os)
{
    /* 
     *  Open a syslog connection
     */
#if SOLARIS
    openlog(mprGetAppName(os), LOG_CONS, LOG_LOCAL0);
#else
    openlog(mprGetAppName(os), LOG_CONS || LOG_PERROR, LOG_LOCAL0);
#endif
    return 0;
}


void mprStopOsService(MprOsService *os)
{
}


int mprGetRandomBytes(MprCtx ctx, char *buf, int length, int block)
{
    int     fd, sofar, rc;

    fd = open((block) ? "/dev/random" : "/dev/urandom", O_RDONLY, 0666);
    if (fd < 0) {
        return MPR_ERR_CANT_OPEN;
    }

    sofar = 0;
    do {
        rc = read(fd, &buf[sofar], length);
        if (rc < 0) {
            mprAssert(0);
            return MPR_ERR_CANT_READ;
        }
        length -= rc;
        sofar += rc;
    } while (length > 0);
    close(fd);
    return 0;
}


MprModule *mprLoadModule(MprCtx ctx, cchar *moduleName, cchar *initFunction)
{
#if BLD_CC_DYN_LOAD
    MprModuleEntry  fn;
    MprModule       *mp;
    char            *path, *module;
    void            *handle;

    mprAssert(moduleName && *moduleName);

    mp = 0;
    path = 0;
    module = mprGetNormalizedPath(ctx, moduleName);

    if (mprSearchForModule(ctx, module, &path) < 0) {
        mprError(ctx, "Can't find module \"%s\" in search path \"%s\"", moduleName, mprGetModuleSearchPath(ctx));
    } else {
        mprLog(ctx, MPR_INFO, "Loading module %s from %s", module, path);
        if ((handle = dlopen(path, RTLD_LAZY | RTLD_GLOBAL)) == 0) {
            mprError(ctx, "Can't load module %s\nReason: \"%s\"",  path, dlerror());
        } else if (initFunction) {
            if ((fn = (MprModuleEntry) dlsym(handle, initFunction)) != 0) {
                if ((mp = (fn)(ctx, path)) == 0) {
                    mprError(ctx, "Initialization for module %s failed", module);
                    dlclose(handle);
                } else {
                    mp->handle = handle;
                }
            } else {
                mprError(ctx, "Can't load module %s\nReason: can't find function \"%s\"",  path, initFunction);
                dlclose(handle);
            }
        }
    }
    mprFree(path);
    mprFree(module);
    return mp;
#else
    mprError(ctx, "Product built without the ability to load modules dynamically");
    return 0;
#endif
}


void mprSleep(MprCtx ctx, int milliseconds)
{
    struct timespec timeout;
    int             rc;

    mprAssert(milliseconds >= 0);
    timeout.tv_sec = milliseconds / 1000;
    timeout.tv_nsec = (milliseconds % 1000) * 1000000;
    do {
        rc = nanosleep(&timeout, &timeout);
    } while (rc < 0 && errno == EINTR);
}


void mprUnloadModule(MprModule *mp)
{
    if (mp->handle) {
        dlclose(mp->handle);
    }
    mprRemoveItem(mprGetMpr(mp)->moduleService->modules, mp);
}


/*  
 *  Write a message in the O/S native log (syslog in the case of linux)
 */
void mprWriteToOsLog(MprCtx ctx, cchar *message, int flags, int level)
{
    char    *msg;
    int     sflag;

    if (flags & MPR_FATAL_SRC) {
        msg = "fatal error: ";
        sflag = LOG_ERR;

    } else if (flags & MPR_ASSERT_SRC) {
        msg = "program assertion error: ";
        sflag = LOG_WARNING;

    } else {
        msg = "error: ";
        sflag = LOG_WARNING;
    }
    syslog(sflag, "%s %s: %s\n", mprGetAppName(ctx), msg, message);
}

#else
void __dummyMprUnix() {}
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
    vim: sw=4 ts=4 expandtab

    @end
 */
