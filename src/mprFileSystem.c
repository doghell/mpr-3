/**
 *  mprFileSystem.c - File system services.
 *
 *  This module provides a simple cross platform file system abstraction. File systems provide a file system switch and 
 *  underneath a file system provider that implements actual I/O.
 *
 *  This module is not thread-safe.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/************************************ Code ************************************/

MprFileSystem *mprCreateFileSystem(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;
    Mpr             *mpr;
    char            *cp;

    mpr = mprGetMpr(ctx);
    mprAssert(mpr);
    
#if BLD_FEATURE_ROMFS
    fs = (MprFileSystem*) mprCreateRomFileSystem(ctx, path);
#elif BREW
    fs = (MprFileSystem*) mprCreateBrewFileSystem(ctx, path);
#else
    fs = (MprFileSystem*) mprCreateDiskFileSystem(ctx, path);
#endif

#if BLD_WIN_LIKE
    fs->separators = mprStrdup(fs, "\\/");
    fs->newline = mprStrdup(fs, "\r\n");
#else
    fs->separators = mprStrdup(fs, "/");
    fs->newline = mprStrdup(fs, "\n");
#endif

#if BLD_WIN_LIKE || MACOSX
    fs->caseSensitive = 0;
#else
    fs->caseSensitive = 1;
#endif

#if BLD_WIN_LIKE || VXWORKS
    fs->hasDriveSpecs = 1;
#endif

    if (mpr->fileSystem == NULL) {
        mpr->fileSystem = fs;
    }
    fs->root = mprGetAbsPath(ctx, path);
    if ((cp = strpbrk(fs->root, fs->separators)) != 0) {
        *++cp = '\0';
    }
    return fs;
}


void mprAddFileSystem(MprCtx ctx, MprFileSystem *fs)
{
    mprAssert(ctx);
    mprAssert(fs);
    
    mprGetMpr(ctx)->fileSystem = fs;
}


/*
 *  Note: path can be null
 */
MprFileSystem *mprLookupFileSystem(MprCtx ctx, cchar *path)
{
    mprAssert(ctx);
    
    return mprGetMpr(ctx)->fileSystem;
}


cchar *mprGetPathNewline(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;

    mprAssert(ctx);
    mprAssert(path);

    fs = mprLookupFileSystem(ctx, path);
    return fs->newline;
}


cchar *mprGetPathSeparators(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;

    mprAssert(ctx);
    mprAssert(path);

    fs = mprLookupFileSystem(ctx, path);
    return fs->separators;
}


void mprSetPathSeparators(MprCtx ctx, cchar *path, cchar *separators)
{
    MprFileSystem   *fs;

    mprAssert(ctx);
    mprAssert(path);
    mprAssert(separators);
    
    fs = mprLookupFileSystem(ctx, path);
    mprFree(fs->separators);
    fs->separators = mprStrdup(fs, separators);
}


void mprSetPathNewline(MprCtx ctx, cchar *path, cchar *newline)
{
    MprFileSystem   *fs;
    
    mprAssert(ctx);
    mprAssert(path);
    mprAssert(newline);
    
    fs = mprLookupFileSystem(ctx, path);
    mprFree(fs->newline);
    fs->newline = mprStrdup(fs, newline);
}


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
