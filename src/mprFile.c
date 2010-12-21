/**
 *  mprFile.c - File services.
 *
 *  This modules provides a simple cross platform file I/O abstraction. It uses the MprFileSystem to provide I/O services.
 *
 *  This module is not thread safe.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/****************************** Forward Declarations **************************/

static int  fillBuf(MprFile *file);

/************************************ Code ************************************/

MprFile *mprAttachFd(MprCtx ctx, int fd, cchar *name, int omode)
{
    MprFileSystem   *fs;
    MprFile         *file;

    fs = mprLookupFileSystem(ctx, "/");

    file = mprAllocObjZeroed(ctx, MprFile);
    if (file) {
        file->fd = fd;
        file->fileSystem = fs;
#if BLD_DEBUG
        file->path = mprStrdup(file, name);
#endif
        file->mode = omode;
    }
    return file;
}


MprFile *mprGetStderr(MprCtx ctx)
{
    MprFileSystem   *fs;
    fs = mprLookupFileSystem(ctx, NULL);
    return fs->stdError;
}


MprFile *mprGetStdin(MprCtx ctx)
{
    MprFileSystem   *fs;
    fs = mprLookupFileSystem(ctx, NULL);
    return fs->stdInput;
}


MprFile *mprGetStdout(MprCtx ctx)
{
    MprFileSystem   *fs;
    fs = mprLookupFileSystem(ctx, NULL);
    return fs->stdOutput;
}


MprFile *mprOpen(MprCtx ctx, cchar *path, int omode, int perms)
{
    MprFileSystem   *fs;
    MprFile         *file;
    MprPath         info;

    fs = mprLookupFileSystem(ctx, path);

    file = fs->openFile(ctx, fs, path, omode, perms);
    if (file) {
        file->fileSystem = fs;
#if BLD_DEBUG
        file->path = mprStrdup(file, path);
#endif
        if (omode & (O_WRONLY | O_RDWR)) {
            /*
             *  OPT. Should compute this lazily.
             */
            fs->getPathInfo(fs, path, &info);
            file->size = (MprOffset) info.size;
        }
        file->mode = omode;
        file->perms = perms;
    }
    return file;
}


int mprRead(MprFile *file, void *buf, uint size)
{
    MprFileSystem   *fs;
    MprBuf          *bp;
    void            *bufStart;
    int             bytes, totalRead;

    mprAssert(file);
    if (file == 0) {
        return MPR_ERR_BAD_HANDLE;
    }

    fs = file->fileSystem;
    bp = file->buf;
    if (bp == 0) {
        totalRead = fs->readFile(file, buf, size);

    } else {
        bufStart = buf;
        while (size > 0) {
            if (mprGetBufLength(bp) == 0) {
                bytes = fillBuf(file);
                if (bytes <= 0) {
                    return -1;
                }
            }
            bytes = min((int) size, mprGetBufLength(bp));
            memcpy(buf, mprGetBufStart(bp), bytes);
            mprAdjustBufStart(bp, bytes);
            buf = (void*) (((char*) buf) + bytes);
            size -= bytes;
        }
        totalRead = (int) ((char*) buf - (char*) bufStart);
    }
    file->pos += totalRead;
    return totalRead;
}


int mprWrite(MprFile *file, cvoid *buf, uint count)
{
    MprFileSystem   *fs;
    MprBuf          *bp;
    int             bytes, written;

    mprAssert(file);
    if (file == 0) {
        return MPR_ERR_BAD_HANDLE;
    }

    fs = file->fileSystem;
    bp = file->buf;
    if (bp == 0) {
        if ((written = fs->writeFile(file, buf, count)) < 0) {
            return written;
        }

    } else {
        written = 0;
        while (count > 0) {
            bytes = mprPutBlockToBuf(bp, buf, count);
            if (bytes < 0) {
                return bytes;
            } 
            if (bytes != (int) count) {
                mprFlush(file);
            }
            count -= bytes;
            written += bytes;
            buf = (char*) buf + bytes;
        }
    }
    file->pos += written;
    if (file->pos > file->size) {
        file->size = file->pos;
    }
    return written;
}


int mprWriteString(MprFile *file, cchar *str)
{
    return mprWrite(file, str, strlen(str));
}


int mprWriteFormat(MprFile *file, cchar *fmt, ...)
{
    va_list     ap;
    char        *buf;
    int         rc;

    rc = -1;
    va_start(ap, fmt);
    if ((buf = mprVasprintf(file, -1, fmt, ap)) != NULL) {
        rc = mprWriteString(file, buf);
    }
    va_end(ap);
    return rc;
}


int mprFlush(MprFile *file)
{
    MprFileSystem   *fs;
    MprBuf          *bp;
    int             len, rc;

    mprAssert(file);
    if (file == 0) {
        return MPR_ERR_BAD_HANDLE;
    }
    if (file->buf == 0) {
        return 0;
    }

    if (file->mode & (O_WRONLY | O_RDWR)) {
        fs = file->fileSystem;
        bp = file->buf;
        while (mprGetBufLength(bp) > 0) {
            len = mprGetBufLength(bp);
            rc = fs->writeFile(file, mprGetBufStart(bp), len);
            if (rc < 0) {
                return rc;
            }
            mprAdjustBufStart(bp, rc);
        }
        mprFlushBuf(bp);
    }
    return 0;
}


long mprSeek(MprFile *file, int seekType, long pos)
{
    MprFileSystem   *fs;

    mprAssert(file);

    fs = file->fileSystem;

    if (file->buf) {
        if (! (seekType == SEEK_CUR && pos == 0)) {
            /*
             *  Discard buffering as we may be seeking outside the buffer.
             *  OPT. Could be smarter about this and preserve the buffer.
             */
            if (file->mode & (O_WRONLY | O_RDWR)) {
                if (mprFlush(file) < 0) {
                    return MPR_ERR_CANT_WRITE;
                }
            }
            if (file->buf) {
                mprFlushBuf(file->buf);
            }
        }
    }

    if (seekType == SEEK_SET) {
        file->pos = pos;

    } else if (seekType == SEEK_CUR) {
        file->pos += pos;

    } else {
        file->pos = fs->seekFile(file, SEEK_END, 0);
    }

    if (fs->seekFile(file, SEEK_SET, (long) file->pos) != (long) file->pos) {
        return MPR_ERR;
    }
    if (file->mode & (O_WRONLY | O_RDWR)) {
        if (file->pos > file->size) {
            file->size = file->pos;
        }
    }
    return (long) file->pos;
}


MprOffset mprGetFilePosition(MprFile *file)
{
    return file->pos;
}


MprOffset mprGetFileSize(MprFile *file)
{
    return file->size;
}


/*
 *  Fill the read buffer. Return the new buffer length. Only called when the buffer is empty.
 */
static int fillBuf(MprFile *file)
{
    MprFileSystem   *fs;
    MprBuf          *bp;
    int             len;

    bp = file->buf;
    fs = file->fileSystem;

    mprAssert(mprGetBufLength(bp) == 0);
    mprFlushBuf(bp);

    len = fs->readFile(file, mprGetBufStart(bp), mprGetBufSpace(bp));
    if (len <= 0) {
        return len;
    }
    mprAdjustBufEnd(bp, len);
    return len;
}


/*
 *  Get a string from the file. This will put the file into buffered mode.
 */
char *mprGets(MprFile *file, char *buf, uint size)
{
    MprBuf          *bp;
    MprFileSystem   *fs;
    int             count, c;

    mprAssert(file);
    if (file == 0) {
        return 0;
    }

    fs = file->fileSystem;
    if (file->buf == 0) {
        file->buf = mprCreateBuf(file, MPR_BUFSIZE, MPR_MAX_STRING);
    }
    bp = file->buf;

    /*
     *  Must leave room for null
     */
    count = 0;
    while (--size > 0) {
        if (mprGetBufLength(bp) == 0) {
            if (fillBuf(file) <= 0) {
                return 0;
            }
        }
        if ((c = mprGetCharFromBuf(bp)) == '\n') {
            file->pos++;
            break;
        }
        buf[count++] = c;
    }
    buf[count] = '\0';
    file->pos += count;
    return buf;
}


/*
 *  Put a string to the file. This will put the file into buffered mode.
 */
int mprPuts(MprFile *file, cchar *str)
{
    MprBuf  *bp;
    char    *buf;
    int     total, bytes, count;

    mprAssert(file);
    count = strlen(str);

    /*
     *  Buffer output and flush when full.
     */
    if (file->buf == 0) {
        file->buf = mprCreateBuf(file, MPR_BUFSIZE, 0);
        if (file->buf == 0) {
            return MPR_ERR_CANT_ALLOCATE;
        }
    }
    bp = file->buf;

    if (mprGetBufLength(bp) > 0 && mprGetBufSpace(bp) < (int) count) {
        mprFlush(file);
    }
    total = 0;
    buf = (char*) str;

    while (count > 0) {
        bytes = mprPutBlockToBuf(bp, buf, count);
        if (bytes < 0) {
            return MPR_ERR_CANT_ALLOCATE;

        } else if (bytes == 0) {
            if (mprFlush(file) < 0) {
                return MPR_ERR_CANT_WRITE;
            }
            continue;
        }
        count -= bytes;
        buf += bytes;
        total += bytes;
        file->pos += bytes;
    }
    return total;
}


/*
 *  Get a character from the file. This will put the file into buffered mode.
 */
int mprGetc(MprFile *file)
{
    MprBuf  *bp;
    int     len;

    mprAssert(file);

    if (file == 0) {
        return MPR_ERR;
    }
    if (file->buf == 0) {
        file->buf = mprCreateBuf(file, MPR_BUFSIZE, MPR_MAX_STRING);
    }
    bp = file->buf;

    if (mprGetBufLength(bp) == 0) {
        len = fillBuf(file);
        if (len <= 0) {
            return -1;
        }
    }
    if (mprGetBufLength(bp) == 0) {
        return 0;
    }
    file->pos++;
    return mprGetCharFromBuf(bp);
}


/*
 *  Peek at a character from the file without disturbing the read position. This will put the file into buffered mode.
 */
int mprPeekc(MprFile *file)
{
    MprBuf  *bp;
    int     len;

    mprAssert(file);

    if (file == 0) {
        return MPR_ERR;
    }

    if (file->buf == 0) {
        file->buf = mprCreateBuf(file, MPR_BUFSIZE, MPR_MAX_STRING);
    }
    bp = file->buf;

    if (mprGetBufLength(bp) == 0) {
        len = fillBuf(file);
        if (len <= 0) {
            return -1;
        }
    }
    if (mprGetBufLength(bp) == 0) {
        return 0;
    }
    return ((uchar*) mprGetBufStart(bp))[0];
}


/*
 *  Put a character to the file. This will put the file into buffered mode.
 */
int mprPutc(MprFile *file, int c)
{
    mprAssert(file);

    if (file == 0) {
        return -1;
    }
    if (file->buf) {
        if (mprPutCharToBuf(file->buf, c) != 1) {
            return MPR_ERR_CANT_WRITE;
        }
        file->pos++;
        return 1;

    }
    return mprWrite(file, &c, 1);
}


/*
 *  Enable and control file buffering
 */
int mprEnableFileBuffering(MprFile *file, int initialSize, int maxSize)
{
    mprAssert(file);

    if (file == 0) {
        return MPR_ERR_BAD_STATE;
    }
    if (initialSize <= 0) {
        initialSize = MPR_BUFSIZE;
    }
    if (maxSize <= 0) {
        maxSize = MPR_BUFSIZE;
    }
    if (maxSize <= initialSize) {
        maxSize = initialSize;
    }
    if (file->buf == 0) {
        file->buf = mprCreateBuf(file, initialSize, maxSize);
    }
    return 0;
}


void mprDisableFileBuffering(MprFile *file)
{
    mprFlush(file);
    mprFree(file->buf);
    file->buf = 0;
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
