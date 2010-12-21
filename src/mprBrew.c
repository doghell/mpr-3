/**
 *  mprBrew.c - Brew specific adaptions
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

#if BREW
/****************************** Forward Declarations **************************/

static void closeFile(MprFile *file);
static int  consoleWrite(MprFile *file, const void *buf, uint count);
static int  brewFileMode(int omode);
static int fileSystemDestructor(MprDiskFileSystem *fs)

/*********************************** Code *************************************/

MprOsService *mprCreateOsService(MprCtx ctx)
{
    return mprAllocObj(ctx, MprOsService);
}


static int osDestructor(MprOsService *os)
{
    return 0;
}


int mprStartOsService(MprOsService *os)
{
    return 0;
}


void mprStopOsService(MprOsService *os)
{
}


int mprGetRandomBytes(MprCtx ctx, char *buf, int length, int block)
{
    MprTime now;
    int     i;

    for (i = 0; i < length; i++) {
        now = mprGetTime(ctx);
        buf[i] = (uchar) (now >> i);
    }
    return 0;
}


void mprWriteToOsLog(MprCtx ctx, cchar *message, int flags, int level)
{
}


void mprSetShell(MprCtx ctx, void *shell)
{
    mprGetMpr(ctx)->shell = shell;
}


void *mprGetShell(MprCtx ctx)
{
    return mprGetMpr(ctx)->shell;
}


void mprSetClassId(MprCtx ctx, uint classId)
{
    mprGetMpr(ctx)->classId = classId;
}


uint mprGetClassId(MprCtx ctx)
{
    return mprGetMpr(ctx)->classId;
}


void mprSetDisplay(MprCtx ctx, void *display)
{
    mprGetMpr(ctx)->display = display;
}


void *mprGetDisplay(MprCtx ctx)
{
    return mprGetMpr(ctx)->display;
}


/*
 *  Sleep. Period given in milliseconds.
 *  WARNING: not a good idea to call this as it will hang the phone !!!!
 */
void mprSleep(MprCtx ctx, int milliseconds)
{
    MprTime     then;

    then = mprGetTime(ctx) + milliseconds;

    while (mprCompareTime(mprGetTime(ctx), then) < 0) {
        ;
    }
}


struct hostent *mprGetHostByName(MprCtx ctx, cchar *name)
{
    return 0;
}


int getpid()
{
    return 0;
}


int isalnum(int c)
{
    return (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || ('0' <= c && c <= '9'));
} 


int isalpha(int c)
{
    return (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z'));
} 


int isdigit(int c)
{
    return ('0' <= c && c <= '9');
}
 

int islower(int c)
{
    return ('a' <= c && c <= 'z');
}
 

int isspace(int c)
{
    return (c == ' ' || c == '\t' || c == '\n' || c == '\r');
}
 

int isupper(int c)
{
    return ('A' <= c && c <= 'Z');
}
 

int isxdigit(int c)
{
    return ('0' <= c && c <= '9') || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
}
 

char *strpbrk(cchar *str, cchar *set)
{
    uchar   charMap[32];
    int     i;

    for (i = 0; i < 32; i++) {
        charMap[i] = 0;
    }

    while (*set) {
        charMap[*set >> 0x3] |= (1 << (*set & 0x7));
        set++;
    }

    while (*str) {
        if (charMap[*str >> 3] & (1 << (*str & 7)))
            return (char*) str;
        str++;
    }
    return 0;
} 


uint strspn(cchar *str, cchar *set)
{
    uchar   charMap[32];
    int     i;

    for (i = 0; i < 32; i++) {
        charMap[i] = 0;
    }

    while (*set) {
        charMap[*set >> 0x3] |= (1 << (*set & 0x7));
        set++;
    }

    if (*str) {
        i = 0;
        while (charMap[*str >> 0x3] & (1 << (*str & 0x7))) {
            i++;
            str++;
        }
        return i;
    }

    return 0;
}
 

char *strstr(cchar *str, cchar *subString)
{
    char *cp, *s1, *s2;

    if (subString == 0 || *subString == '\0') {
        return (char*) str;
    }

    for (cp = (char*) str; *cp; cp++) {
        s1 = cp;
        s2 = (char *) subString;

        while (*s1 && *s2 && (*s1 == *s2)) {
            s1++;
            s2++;
        }

        if (*s2 == '\0') {
            return cp;
        }
    }
    return 0;
}


/********************************* BREW  Wrapping *****************************/
#if !BREWSIM

uint strlen(cchar *str)
{
    return STRLEN(str);
}


void *memset(const void *dest, int c, uint count)
{
    return MEMSET((void*) dest, c, count);
}


int toupper(int c)
{
    if (islower(c)) {
        c = 'A' + c - 'a';
    }
    return c;
}


void *memcpy(void *dest, const void *src, uint count)
{
    return MEMCPY(dest, src, count);
}


/*
 *  Copy memory supporting overlapping regions
 */

void *memmove(void *destPtr, const void *srcPtr, uint count)
{
    char    *dest, *src;

    dest = (char*) destPtr;
    src = (char*) srcPtr;

    if (dest <= src || dest >= &src[count]) {
        /*
         *  Disjoint
         */
        while (count--) {
            *dest++ = *src++;
        }

    } else {
        /*
         * Overlapping region
         */
        dest = &dest[count - 1];
        src = &src[count - 1];

        while (count--) {
            *dest-- = *src--;
        }
    }
    return destPtr;
}


char *strrchr(cchar *str, int c)
{
    return STRRCHR(str, c);
}


char *strcat(char *dest, cchar *src)
{
    return STRCAT(dest, src);
}


int strcmp(cchar *s1, cchar *s2)
{
    return STRCMP(s1, s2);
}


int strncmp(cchar *s1, cchar *s2, uint count)
{
    return STRNCMP(s1, s2, count);
}


char *strcpy(char *dest, cchar *src)
{
    return STRCPY(dest, src);
}


char *strncpy(char *dest, cchar *src, uint count)
{
    return STRNCPY(dest, src, count);
}


char *strchr(cchar *str, int c)
{
    return STRCHR(str, c);
}


int atoi(cchar *str)
{
    return ATOI(str);
}


int tolower(int c)
{
    if (isupper(c)) {
        c = 'a' + c - 'A';
    }
    return c;
}


void *malloc(uint size)
{
    void    *ptr;
    ptr = MALLOC(size);
    if (ptr == 0) {
        mprAssert(0);
    }
    return ptr; 
}


void *realloc(void *ptr, uint size)
{
    void    *newPtr;

    newPtr = REALLOC(ptr, size);
    if (newPtr == 0) {
        mprAssert(0);
    }
    return newPtr;
}


void free(void *ptr)
{
    if (ptr) {
        FREE(ptr);
    }
}
#endif /* ! BREWSIM */


static MprFile *openFile(MprCtx ctx, MprFileSystem *fileSystem, cchar *path, int omode, int perms)
{
    MprBrewFileSystem   *bfs;
    MprFile             *file;

    mprAssert(path && *path);

    bfs = (MprBrewFileSystem*) fileSystem;
    file = mprAllocObjWithDestructorZeroed(ctx, MprFile, closeFile);
    if (file == 0) {
        mprAssert(file);
        return 0;
    }
    file->mode = omode;

    if (omode & O_CREAT) {
        IFILEMGR_Remove(fileSystem->fileMgr, path);
    }
    file->fd = IFILEMGR_OpenFile(fileSystem->fileMgr, path, brewFileMode(omode));
    if (file->fd == 0) {
        /* int err = IFILEMGR_GetLastError(fileSystem->fileMgr); */
        return 0;
    }

    return file;
}


static int closeFile(MprFile *file)
{
    mprAssert(file);

    if (file == 0) {
        return;
    }
    mprFlush(file);
    IFILE_Release(file->fd);
    return 0;
}
 

static int readFile(MprFile *file, void *buf, uint size)
{
    mprAssert(file);
    mprAssert(buf);

    if (file == 0) {
        return MPR_ERR_BAD_HANDLE;
    }
    return IFILE_Read(file->fd, buf, size);
}


static int writeFile(MprFile *file, const void *buf, uint count)
{
    mprAssert(file);
    mprAssert(buf);

    if (file == 0) {
        return MPR_ERR_BAD_HANDLE;
    }

    /*
     *  Handle == 1 is for console writes
     */
    if (file->fd == (IFile*) 1) {
        return consoleWrite(file, buf, count);
    }
    return IFILE_Write(file->fd, buf, count);
}


static int seekFile(MprFile *file, int seekType, long distance)
{
    int     type;

    mprAssert(file);

    if (file == 0) {
        return MPR_ERR_BAD_HANDLE;
    }

    if (seekType == SEEK_SET) {
        type = _SEEK_START;
    } else if (seekType == SEEK_END) {
        type = _SEEK_END;
    } else {
        type = _SEEK_CURRENT;
    }
    return IFILE_Seek(file->fd, type, distance);
}


static bool accessPath(MprBrewFileSystem *fileSystem, cchar *path, int omode)
{
    return getPathInfo(fileSystem, path);
}


static int deletePath(MprBrewFileSystem *fileSystem, cchar *path)
{
    FileInfo    info;

    getPathInfo(fileSystem, path, &info);
    if (info.valid && info.isDir) {
        if (IFILEMGR_RmDir(fileSystem->fileMgr, path) == EFAILED) {
            mprError(ctx, "Can't remove directory %s, error %d", path, IFILEMGR_GetLastError(fileSystem->fileMgr));
            return MPR_ERR_CANT_ACCESS;
        }
    } else {
        if (IFILEMGR_Remove(fileSystem->fileMgr, path) == EFAILED) {
            return MPR_ERR_CANT_ACCESS;
        }
    }
    return 0;
}
 

static int makeDir(MprBrewFileSystem *fileSystem, cchar *path, int perms)
{
    if (IFILEMGR_MkDir(fileSystem->fileMgr, path) == EFAILED) {
        mprError(ctx, "Can't make directory %s, error %d", path, IFILEMGR_GetLastError(fileSystem->fileMgr));
        return MPR_ERR_CANT_ACCESS;
    }
    return 0;
}
 

static int getPathInfo(MprBrewFileSystem *fileSystem, cchar *path, MprPath *info)
{
    FileInfo        brewFileInfo;

    mprAssert(path && *path);
    mprAssert(info);

    info->checked = 1;
    info->valid = 0;
    if (IFILEMGR_GetInfo(fileSystem->fileMgr, path, &brewFileInfo) == EFAILED) {
        mprError(ctx, "Can't get file info for %s, error %d", path, 
            IFILEMGR_GetLastError(fileSystem->fileMgr));
        return -1;
    }

    info->size = brewFileInfo.dwSize;
    info->ctime = brewFileInfo.dwCreationDate;
    info->isDir = brewFileInfo.attrib & _FA_DIR;
    info->isReg = brewFileInfo.attrib & _FA_NORMAL;
    info->valid = 1;
    info->checked = 1;

    return 0;
}
 

static int consoleWrite(MprFile *file, const void *writeBuf, uint count)
{
    MprBuf  *bp;
    char    *start, *cp, *end, *np, *buf;
    int     total, bytes;

    mprAssert(file);
    mprAssert(writeBuf);

    /*
     *  Buffer output and flush on a '\n'. This is necesary because 
     *  BREW appends a new line to all calls to DBGPRINTF.
     */
    if (file->buf == 0) {
#if BREWSIM
        file->buf = mprCreateBuf(file, 128, 128);
#else
        file->buf = mprCreateBuf(file, 35, 35);
#endif
    }
    bp = file->buf;

    if (mprGetBufLength(bp) > 0 && mprGetBufSpace(bp) < (int) count) {
        printf(" MP: %s", mprGetBufStart(bp));
        mprFlushBuf(bp);
    }

    total = 0;
    buf = (char*) writeBuf;

    while (count > 0) {
        bytes = mprPutBlockToBuf(bp, buf, count);
        buf += bytes;
        count -= bytes;
        total += bytes;

        /*
         *  Output the line if we find a newline or the line is too long to 
         *  buffer (count > 0).
         */
        if (strchr(mprGetBufStart(bp), '\n') || count > 0) {
            end = cp = mprGetBufEnd(bp);
            start = cp = mprGetBufStart(bp);

            /*
             *  Brew can't handle tabs
             */
            for (; cp < end && *cp; cp++) {
                if (*cp == '\t') {
                    *cp = ' ';
                }
            }

            cp = start;
            for (np = cp; np < end; np++) {
                if (*np == '\n') {
                    *np = '\0';
                    /* BREW appends '\n' */
                    if (count > 0) {
                        printf("_MP: %s", cp);
                    } else {
                        printf(" MP: %s", cp);
                    }
                    cp = np + 1;
                }
            }
            if (cp < np) {
                if (cp == start) {
                    /* Nothing output. Line must be too long */
                    printf("_MP: %s", cp);
                    mprFlushBuf(bp);

                } else if (count > 0) {
                    /* We did output text, but there is more of this line */
                    mprAdjustBufStart(bp, (int) (cp - start));
                    mprCompactBuf(bp);

                } else {
                    printf(" MP: %s", cp);
                    mprFlushBuf(bp);
                }
            } else {
                mprFlushBuf(bp);
            }
        }
    }
    return total;
}


void mprSetFileMgr(MprCtx ctx, void *fileMgr)
{
    MprFileSystem       *fs;

    fs = mprLookupFileSystem(ctx, "/");
    fs->fileMgr = fileMgr;
}


void *mprGetFileMgr(MprCtx ctx)
{
    MprFileSystem       *fs;

    fs = mprLookupFileSystem(ctx, "/");
    return fs->fileMgr;
}


static int brewFileMode(int omode)
{
    uint        mode;

    mode = 0;

    if (omode & (O_RDONLY | O_RDWR)) {
        mode |= _OFM_READ;
    }
    if (omode & (O_RDWR)) {
        mode |= _OFM_READWRITE;
    }
    if (omode & (O_CREAT)) {
        mode |= _OFM_CREATE;
    }
    if (omode & (O_APPEND)) {
        mode |= _OFM_APPEND;
    }
    return mode;
}


int *mprSetBrewFileSystem(MprCtx ctx)
{
    MprBrewFileSystem  *bfs;

    /*
     *  Assume that STDOUT is 1 and STDERR is 2
     */
    bfs->stdOutput = mprAllocObjWithDestructorZeroed(bfs, MprFile);
    bfs->stdError = mprAllocObjWithDestructorZeroed(bfs, MprFile);

    bfs->stdOutput->fd = (IFile*) 1;
    bfs->error->fd = (IFile*) 2;

    bfs->stdOutput->fs = (MprFileSystem*) bfs;
    bfs->stdError->fs = (MprFileSystem*) bfs;

    bfs->stdOutput->mode = O_WRONLY;
    bfs->stdError->mode = O_WRONLY;

    mprAssert(bfs->fileMgr);
    if (ISHELL_CreateInstance(mprGetMpr(bfs)->shell, AEECLSID_FILEMGR, (void**) &bfs->fileMgr) != SUCCESS) {
        mprError(fs, "Can't open file manager.");
        return MPR_ERR_CANT_OPEN;
    }
    return fs;
}


MprBrewFileSystem *mprCreateBrewFileSystem(MprCtx ctx, cchar *path)
{
    MprBrewFileSystem  *bfs;

    bfs = mprAllocObjWithDestructorZeroed(ctx, MprBrewFileSystem, fileSystemDestructor);
    if (bfs == 0) {
        return 0;
    }

    bfs->accessPath = accessPath;
    bfs->deletePath = deletePath;
    bfs->getPathInfo = getPathInfo;
    bfs->makeDir = makeDir;
    bfs->openFile = openFile;
    bfs->closeFile = closeFile;
    bfs->readFile = readFile;
    bfs->seekFile = seekFile;
    bfs->writeFile = writeFile;

    return bfs;
}


static int fileSystemDestructor(MprDiskFileSystem *fs)
{
    IFILEMGR_Release(fs->fileMgr);
    return 0;
}


#else
void __dummyBrew() {}
#endif /* !BREW */

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
