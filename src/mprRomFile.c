/*
 *  mprRomFile.c - ROM File system
 *
 *  ROM support for systems without disk or flash based file systems. This module provides read-only file retrieval 
 *  from compiled file images. Use the mprRomComp program to compile files into C code and then link them into your 
 *  application. This module uses a hashed symbol table for fast file lookup.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

#if BLD_FEATURE_ROMFS 
/****************************** Forward Declarations **************************/

static int closeFile(MprFile *file);
static int getPathInfo(MprRomFileSystem *rfs, cchar *path, MprPath *info);
static MprRomInode *lookup(MprRomFileSystem *rfs, cchar *path);

/*********************************** Code *************************************/

static MprFile *openFile(MprCtx ctx, MprFileSystem *fileSystem, cchar *path, int flags, int omode)
{
    MprRomFileSystem    *rfs;
    MprFile             *file;
    
    mprAssert(path && *path);

    rfs = (MprRomFileSystem*) fileSystem;
    file = mprAllocObjWithDestructorZeroed(ctx, MprFile, closeFile);
    file->fileSystem = fileSystem;
    file->mode = omode;
    file->fd = -1;

    if ((file->inode = lookup(rfs, path)) == 0) {
        return 0;
    }
    return file;
}


static int closeFile(MprFile *file)
{
    return 0;
}


static int readFile(MprFile *file, void *buf, uint size)
{
    MprRomInode     *inode;
    int             len;

    mprAssert(buf);

    if (file->fd == 0) {
        return read(file->fd, buf, size);
    }
    inode = file->inode;
    len = min(inode->size - file->iopos, size);
    mprAssert(len >= 0);
    memcpy(buf, &inode->data[file->iopos], len);
    file->iopos += len;
    return len;
}


static int writeFile(MprFile *file, const void *buf, uint size)
{
    if (file->fd == 1 || file->fd == 2) {
        return write(file->fd, buf, size);
    }
    return MPR_ERR_CANT_WRITE;
}


static long seekFile(MprFile *file, int seekType, long distance)
{
    MprRomInode     *inode;

    mprAssert(seekType == SEEK_SET || seekType == SEEK_CUR || seekType == SEEK_END);

    inode = file->inode;

    switch (seekType) {
    case SEEK_CUR:
        file->iopos += distance;
        break;

    case SEEK_END:
        file->iopos = inode->size + distance;
        break;

    default:
        file->iopos = distance;
        break;
    }
    if (file->iopos < 0) {
        errno = EBADF;
        return MPR_ERR_BAD_STATE;
    }
    return file->iopos;
}


static bool accessPath(MprRomFileSystem *fileSystem, cchar *path, int omode)
{
    MprPath     info;

    return getPathInfo(fileSystem, path, &info) == 0 ? 1 : 0;
}


static int deletePath(MprRomFileSystem *fileSystem, cchar *path)
{
    return MPR_ERR_CANT_WRITE;
}
 

static int makeDir(MprRomFileSystem *fileSystem, cchar *path, int perms)
{
    return MPR_ERR_CANT_WRITE;
}


static int makeLink(MprRomFileSystem *fileSystem, cchar *path, cchar *target, int hard)
{
    return MPR_ERR_CANT_WRITE;
}


static int getPathInfo(MprRomFileSystem *rfs, cchar *path, MprPath *info)
{
    MprRomInode *ri;

    mprAssert(path && *path);

    info->checked = 1;

    if ((ri = (MprRomInode*) lookup(rfs, path)) == 0) {
        return MPR_ERR_NOT_FOUND;
    }
    memset(info, 0, sizeof(MprPath));

    info->valid = 1;
    info->size = ri->size;
    info->mtime = 0;
    info->inode = ri->num;

    if (ri->data == 0) {
        info->isDir = 1;
        info->isReg = 0;
    } else {
        info->isReg = 1;
        info->isDir = 0;
    }
    return 0;
}


static int getPathLink(MprRomFileSystem *rfs, cchar *path)
{
    /* Links not supported on ROMfs */
    return NULL;
}


static MprRomInode *lookup(MprRomFileSystem *rfs, cchar *path)
{
    if (path == 0) {
        return 0;
    }

    /*
     *  Remove "./" segments
     */
    while (*path == '.') {
        if (path[1] == '\0') {
            path++;
        } else if (path[1] == '/') {
            path += 2;
        } else {
            break;
        }
    }

    /*
     *  Skip over the leading "/"
     */
    if (*path == '/') {
        path++;
    }
    return (MprRomInode*) mprLookupHash(rfs->fileIndex, path);
}


int mprSetRomFileSystem(MprCtx ctx, MprRomInode *inodeList)
{
    MprRomFileSystem   * rfs;
    MprRomInode         *ri;

    rfs = (MprRomFileSystem*) mprGetMpr(ctx)->fileSystem;

    rfs->romInodes = inodeList;
    rfs->fileIndex = mprCreateHash(rfs, MPR_FILES_HASH_SIZE);

    for (ri = inodeList; ri->path; ri++) {
        if (mprAddHash(rfs->fileIndex, ri->path, ri) < 0) {
            return MPR_ERR_NO_MEMORY;
        }
    }
    return 0;
}


MprRomFileSystem *mprCreateRomFileSystem(MprCtx ctx, cchar *path)
{
    MprFileSystem      *fs;
    MprRomFileSystem   *rfs;

    rfs = mprAllocObjZeroed(ctx, MprRomFileSystem);
    if (rfs == 0) {
        return rfs;
    }

    fs = &rfs->fileSystem;
    fs->accessPath = (MprAccessFileProc) accessPath;
    fs->deletePath = (MprDeleteFileProc) deletePath;
    fs->getPathInfo = (MprGetPathInfoProc) getPathInfo;
    fs->getPathLink = (MprGetPathLinkProc) getPathLink;
    fs->makeDir = (MprMakeDirProc) makeDir;
    fs->makeLink = (MprMakeDirProc) makeLink;
    fs->openFile = (MprOpenFileProc) openFile;
    fs->closeFile = closeFile;
    fs->readFile = readFile;
    fs->seekFile = seekFile;
    fs->writeFile = writeFile;

#if !WINCE
    fs->stdError = mprAllocObjZeroed(fs, MprFile);
    if (fs->stdError == 0) {
        mprFree(fs);
    }
    fs->stdError->fd = 2;
    fs->stdError->fileSystem = fs;
    fs->stdError->mode = O_WRONLY;

    fs->stdInput = mprAllocObjZeroed(fs, MprFile);
    if (fs->stdInput == 0) {
        mprFree(fs);
    }
    fs->stdInput->fd = 0;
    fs->stdInput->fileSystem = fs;
    fs->stdInput->mode = O_RDONLY;

    fs->stdOutput = mprAllocObjZeroed(fs, MprFile);
    if (fs->stdOutput == 0) {
        mprFree(fs);
    }
    fs->stdOutput->fd = 1;
    fs->stdOutput->fileSystem = fs;
    fs->stdOutput->mode = O_WRONLY;
#endif
    return rfs;
}


#else /* BLD_FEATURE_ROMFS */
void __dummy_romfs() {}
#endif /* BLD_FEATURE_ROMFS */

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
