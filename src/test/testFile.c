/**
 *  testFile.c - Unit tests for the mprFile module
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

/*********************************** Locals **********************************/

#define NAME        "testFile.tmp"
#define FILEMODE    0644

typedef struct MprTestFile {
    char        *name;
} MprTestFile;

/************************************ Code ************************************/
/*
 *  Make a unique filename for a given thread
 */
static char *makePath(MprCtx ctx, cchar *name)
{
    char    *path;

#if BLD_FEATURE_MULTITHREAD
    if ((path = mprAsprintf(ctx, -1, "%s-%d-%s", name, getpid(), mprGetCurrentThreadName(ctx))) == 0) {
        return 0;
    }
#else
    path = mprStrdup(ctx, name);
#endif
    return path;
}


/*
 *  Initialization for this test module
 */
static int initFile(MprTestGroup *gp)
{
    MprTestFile     *ts;

    gp->data = mprAllocObjZeroed(gp, MprTestFile);
    if (gp->data == 0) {
        return MPR_ERR_NO_MEMORY;
    }
    ts = (MprTestFile*) gp->data;

    ts->name = makePath(gp, NAME);
    if (ts->name == 0) {
        mprFree(gp->data);
        gp->data = 0;
        return MPR_ERR_NO_MEMORY;
    }

    /*
     *  Don't mind if these fail. We are just making sure they don't exist before we start the tests.
     */
    mprDeletePath(gp, ts->name);
    return 0;
}


static int termFile(MprTestGroup *gp)
{
    mprFree(gp->data);
    gp->data = 0;
    return 0;
}


static void testBasicIO(MprTestGroup *gp)
{
    MprFile         *file;
    MprPath         info;
    char            buf[512];
    int             pos, len, rc;
    MprTestFile     *ts;

    ts = (MprTestFile*) gp->data;
    
    rc = mprDeletePath(gp, ts->name);
    assert(!mprPathExists(ts, ts->name, R_OK));
    
    file = mprOpen(gp, ts->name, O_CREAT | O_TRUNC | O_RDWR, FILEMODE);
    assert(file != 0);
    assert(mprPathExists(ts, ts->name, R_OK));

    len = mprWrite(file, "abcdef", 6);
    assert(len == 6);
    mprFree(file);

    assert(mprPathExists(ts, ts->name, R_OK));
    rc = mprGetPathInfo(gp, ts->name, &info);
    assert(rc == 0);

    if (info.size != 6) {
        mprSleep(gp, 2000);
        rc = mprGetPathInfo(gp, ts->name, &info);
    }
    assert(info.size == 6);
    assert(!info.isDir);
    assert(info.isReg);
    
    file = mprOpen(gp, ts->name, O_RDWR, FILEMODE);
    assert(file != 0);

    pos = mprSeek(file, SEEK_SET, 1);
    len = mprRead(file, buf, sizeof(buf));
    assert(len == 5);
    buf[len] = '\0';
    
    assert(strcmp(buf, "bcdef") == 0);

    pos = mprSeek(file, SEEK_SET, 0);
    assert(pos == 0);
    assert(mprGetFilePosition(file) == 0);

    len = mprWriteString(file, "Hello\nWorld\n");
    assert(len == 12);
    mprSeek(file, SEEK_SET, 0);

    rc = mprRead(file, buf, sizeof(buf));
    assert(rc == 12);
    assert(mprGetFilePosition(file) == 12);
    buf[12] = '\0';
    assert(strcmp(buf, "Hello\nWorld\n") == 0);
    mprFree(file);

    rc = mprDeletePath(gp, ts->name);
    assert(rc == 0);
    assert(!mprPathExists(ts, ts->name, R_OK));
}
    

static void testBufferedIO(MprTestGroup *gp)
{
    MprFile         *file;
    MprPath         info;
    char            buf[512], *str;
    int             pos, len, rc, c;
    MprTestFile     *ts;

    ts = (MprTestFile*) gp->data;
    
    rc = mprDeletePath(gp, ts->name);
    assert(!mprPathExists(ts, ts->name, R_OK));
    
    file = mprOpen(gp, ts->name, O_CREAT | O_TRUNC | O_RDWR | O_BINARY, FILEMODE);
    assert(file != 0);
    assert(mprPathExists(ts, ts->name, R_OK));
    
    mprEnableFileBuffering(file, 0, 512);
    
    len = mprWrite(file, "abc", 3);
    assert(len == 3);
    
    len = mprPutc(file, 'd');
    assert(len == 1);
    
    len = mprPuts(file, "ef\n");
    assert(len == 3);
    
    assert(mprPathExists(ts, ts->name, R_OK));
    
    /*
     *  No data flushed yet so the length should be zero
     */
    rc = mprGetPathInfo(gp, ts->name, &info);
    assert(rc == 0);
    assert(info.size == 0);
    
    rc = mprFlush(file);
    assert(rc == 0);
    mprFree(file);
    
    /*
     *  Now the length should be set
     */
    rc = mprGetPathInfo(gp, ts->name, &info);
    assert(rc == 0);
    if (info.size != 7) {
        mprSleep(gp, 2000);
        rc = mprGetPathInfo(gp, ts->name, &info);
    }
    assert(info.size == 7);

    file = mprOpen(gp, ts->name, O_RDONLY | O_BINARY, FILEMODE);
    assert(file != 0);
    
    pos = mprSeek(file, SEEK_SET, 0);
    assert(mprPeekc(file) == 'a');
    c = mprGetc(file);
    assert(c == 'a');
    str = mprGets(file, buf, sizeof(buf));
    assert(str != 0);
    len = strlen(str);
    
    assert(len == 5);
    buf[len] = '\0';
    
    assert(strcmp(buf, "bcdef") == 0);
    mprFree(file);

    mprDeletePath(gp, ts->name);
    assert(!mprPathExists(ts, ts->name, R_OK));
}
    

MprTestDef testFile = {
    "file", 0, initFile, termFile,
    {
        MPR_TEST(0, testBasicIO),
        MPR_TEST(0, testBufferedIO),
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
    vim: sw=8 ts=8 expandtab

    @end
 */
