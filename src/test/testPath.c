/**
 *  testPath.c - Unit tests for the mprPath module
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

/*********************************** Locals **********************************/

#define DIR1        "/tmp/testTempDir"
#define DIR2        "/testSubDir"
#define DIRMODE     0755

typedef struct MprTestPath {
    char            *dir1;
    char            *dir2;
} MprTestPath;

/************************************ Code ************************************/
/*
 *  Make a unique filename for a given thread
 */
static char *makePath(MprCtx ctx, cchar *name)
{
    return mprAsprintf(ctx, -1, "%s-%d-%s", name, getpid(), mprGetCurrentThreadName(ctx));
}


/*
 *  Initialization for this test module
 */
static int initPath(MprTestGroup *gp)
{
    MprTestPath     *ts;

    gp->data = mprAllocObjZeroed(gp, MprTestPath);
    if (gp->data == 0) {
        return MPR_ERR_NO_MEMORY;
    }
    ts = (MprTestPath*) gp->data;

    ts->dir1 = makePath(gp, DIR1);
    ts->dir2 = mprAsprintf(gp, -1, "%s%s", ts->dir1, DIR2);
    if (ts->dir1 == 0 || ts->dir2 == 0) {
        mprFree(gp->data);
        gp->data = 0;
        return MPR_ERR_NO_MEMORY;
    }

    /*
     *  Don't mind if these fail. We are just making sure they don't exist before we start the tests.
     */
    rmdir(ts->dir2);
    rmdir(ts->dir1);
    return 0;
}


static int termPath(MprTestGroup *gp)
{
    mprFree(gp->data);
    gp->data = 0;
    return 0;
}


static void testCopyPath(MprTestGroup *gp)
{
    MprTestPath     *ts;
    MprFile         *file;
    char            *from, *to;

    ts = (MprTestPath*) gp->data;

    from = mprAsprintf(gp, -1, "copyTest-%s.tmp", mprGetCurrentThreadName(gp));
    assert(from != 0);
    file = mprOpen(gp, from, O_CREAT | O_TRUNC | O_WRONLY, 0664);
    assert(file != 0);
    mprWriteString(file, "Hello World");
    mprFree(file);

    to = mprAsprintf(gp, -1, "newTest-%s.tmp", mprGetCurrentThreadName(gp));
    assert(mprPathExists(gp, from, F_OK));
    assert(!mprPathExists(gp, to, F_OK));
    mprCopyPath(gp, from, to, 0664);
    assert(mprPathExists(gp, to, F_OK));
    mprDeletePath(gp, from);
    mprDeletePath(gp, to);
}


static void testAbsPath(MprTestGroup *gp)
{
    MprCtx      ctx;
    char        *path;

    ctx = mprAlloc(gp, 1);

#if MANUAL_TESTING
    path = mprGetNormalizedPath(ctx, "/");
    path = mprGetNormalizedPath(ctx, "C:/");
    path = mprGetNormalizedPath(ctx, "C:/abc");
    path = mprGetNormalizedPath(ctx, "");
    path = mprGetNormalizedPath(ctx, "c:abc");
    path = mprGetNormalizedPath(ctx, "abc");

    path = mprGetAbsPath(ctx, "/");
    path = mprGetAbsPath(ctx, "C:/");
    path = mprGetAbsPath(ctx, "C:/abc");
    path = mprGetAbsPath(ctx, "");
    path = mprGetAbsPath(ctx, "c:abc");
    path = mprGetAbsPath(ctx, "abc");
#endif

    path = mprGetAbsPath(ctx, "");
    assert(mprIsAbsPath(ctx, path));
    
    path = mprGetAbsPath(ctx, "/");
    assert(mprIsAbsPath(ctx, path));
    
    path = mprGetAbsPath(ctx, ".../../../../../../../../../../..");
    assert(mprIsAbsPath(ctx, path));
    assert(mprIsAbsPath(ctx, mprGetAbsPath(ctx, "Makefile")));

    /*
     *  Manually check incase mprIsAbs gets it wrong
     */
    path = mprGetAbsPath(ctx, "Makefile");
    assert(path && *path);
    assert(mprIsAbsPath(ctx, path));
#if BLD_WIN_LIKE
    assert(isalpha(path[0]));
    assert(path[1] == ':' && path[2] == '\\');
#elif BLD_UNIX_LIKE
    assert(path[0] == '/' && path[1] != '/');
#endif
    assert(strcmp(mprGetPathBase(ctx, path), "Makefile") == 0);

    mprFree(ctx);
}


static void testJoinPath(MprTestGroup *gp)
{
    MprCtx  ctx;

    ctx = mprAlloc(gp, 1);
    assert(strcmp(mprJoinPath(ctx, "/tmp", "Makefile"), "/tmp/Makefile") == 0);
    assert(strcmp(mprJoinPath(ctx, "/tmp", "/Makefile"), "/Makefile") == 0);
    assert(strcmp(mprJoinPath(ctx, "/tmp", NULL), "/tmp") == 0);
    assert(strcmp(mprJoinPath(ctx, "/tmp", "."), "/tmp") == 0);
    assert(strcmp(mprJoinPath(ctx, "///tmp///", "Makefile///"), "/tmp/Makefile") == 0);
    assert(strcmp(mprJoinPath(ctx, "", "Makefile"), "Makefile") == 0);

    assert(strcmp(mprJoinPathExt(ctx, "/abc", ".exe"), "/abc.exe") == 0);
    assert(strcmp(mprJoinPathExt(ctx, "/abc.bat", ".exe"), "/abc.bat") == 0);
    assert(strcmp(mprJoinPathExt(ctx, "/abc", ""), "/abc") == 0);
    assert(strcmp(mprJoinPathExt(ctx, "ejs.web/file", ".mod"), "ejs.web/file.mod") == 0);

    mprFree(ctx);
}


static void testMakeDir(MprTestGroup *gp)
{
    MprTestPath     *ts;
    int             rc;
    
    ts = (MprTestPath*) gp->data;

    mprGlobalLock(gp);
    rc = mprMakeDir(gp, ts->dir1, DIRMODE, 1);
    assert(rc == 0);
    assert(access(ts->dir1, X_OK) == 0);

    rmdir(ts->dir1);
    assert(access(ts->dir1, X_OK) < 0);

    rmdir(ts->dir2);
    rmdir(ts->dir1);

    /*
     *  Dir should not exist before test
     */
    assert(access(ts->dir1, X_OK) < 0);

    /*
     *  Make directory path
     */
    rc = mprMakeDir(gp, ts->dir2, DIRMODE, 1);
    assert(rc == 0);
    assert(access(ts->dir2, X_OK) == 0);

    rc = mprDeletePath(gp, ts->dir2);
    assert(rc == 0);
    rc = mprDeletePath(gp, ts->dir1);
    assert(rc == 0);
    
    assert(access(ts->dir1, X_OK) < 0);
    assert(access(ts->dir2, X_OK) < 0);
    mprGlobalUnlock(gp);
}


static void testNormalize(MprTestGroup *gp)
{
    MprCtx      ctx;
    char        *path;

    ctx = mprAlloc(gp, 1);
    assert(strcmp(mprGetNormalizedPath(ctx, ""), "") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "/"), "/") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "."), ".") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "abc/"), "abc") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "./"), ".") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "../"), "..") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "/.."), "/") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "./.."), "..") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "//.."), "/") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "/abc/.."), "/") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "abc/.."), ".") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "/abc/../def"), "/def") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "/abc/../def/../xyz"), "/xyz") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "/abc/def/../.."), "/") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "/abc/def/../../xyz"), "/xyz") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "/abc/def/.././../xyz"), "/xyz") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "//a//b//./././c/d/e/f/../../g"), "/a/b/c/d/g") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "../../modules/ejs.mod"), "../../modules/ejs.mod") == 0);

    path = mprGetNormalizedPath(ctx, "//a//b//./././c/d/e/f/../../g");
    mprAssert(strcmp(path, "/a/b/c/d/g") == 0);

#if VXWORKS || BLD_WIN_LIKE
    assert(strcmp(mprGetNormalizedPath(ctx, "host:"), "host:.") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "host:/"), "host:/") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "host:////"), "host:/") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "host:abc"), "host:abc") == 0);
    assert(strcmp(mprGetNormalizedPath(ctx, "c:abc"), "c:abc") == 0);
#endif
}


static void testRelPath(MprTestGroup *gp)
{
    MprCtx  ctx;
    char    *path, *absPath;
    
    ctx = mprAlloc(gp, 1);

    path = mprGetRelPath(ctx, "Makefile");
    assert(strcmp(path, "Makefile") == 0);
    
    path = mprGetNormalizedPath(ctx, "../a.b");
    assert(strcmp(path, "../a.b") == 0);

    path = mprGetRelPath(ctx, "/");
    assert(mprIsRelPath(ctx, path));
    assert(strncmp(path, "../", 3) == 0);
    
    path = mprGetRelPath(ctx, "//");
    assert(mprIsRelPath(ctx, path));
    assert(strncmp(path, "../", 3) == 0);
    
    path = mprGetRelPath(ctx, "/tmp");
    assert(mprIsRelPath(ctx, path));
    assert(strncmp(path, "../", 3) == 0);

    path = mprGetRelPath(ctx, "/Unknown/someone/junk");
    assert(mprIsRelPath(ctx, path));
    assert(strncmp(path, "../", 3) == 0);
           
    path = mprGetRelPath(ctx, "/Users/mob/junk");
    assert(mprIsRelPath(ctx, path));
    assert(strncmp(path, "../", 3) == 0);
    
    path = mprGetRelPath(ctx, "/Users/mob/././../mob/junk");
    assert(mprIsRelPath(ctx, path));
    assert(strncmp(path, "../", 3) == 0);
    
    path = mprGetRelPath(ctx, ".");
    assert(strcmp(path, ".") == 0);

    path = mprGetRelPath(ctx, "..");
    assert(strcmp(path, "..") == 0);

    absPath = mprGetAbsPath(ctx, "Makefile");
    assert(mprIsAbsPath(ctx, absPath));
    path = mprGetRelPath(ctx, absPath);
    assert(!mprIsAbsPath(ctx, path));
    assert(strcmp(path, "Makefile") == 0);
    
    mprFree(ctx);
}


static void testSame(MprTestGroup *gp)
{
    MprCtx  ctx;

    ctx = mprAlloc(gp, 1);

    /* Assumes running in test/utest/api */
    assert(mprSamePath(ctx, "testFile", "./testFile"));
#if AVOID
    /*
     *  This forces the tests to be run in a specific directory
     */
    assert(mprSamePath(ctx, "testFile", "../test/api/testFile"));
    assert(mprSamePath(ctx, mprGetAbsPath(ctx, "testFile"), "../test/api/testFile"));
    assert(mprSamePath(ctx, "testFile", mprGetAbsPath(ctx, "../test/api/testFile")));
#endif
}


static void testSearch(MprTestGroup *gp)
{
    MprCtx  ctx;

    ctx = mprAlloc(gp, 1);

    assert(mprSearchPath(ctx, "Makefile", 0, "../.." MPR_SEARCH_SEP ".", NULL) != 0);
    assert(mprSearchPath(ctx, "Makefile", 0, "../..", ".", NULL) != 0);
    assert(!mprSearchPath(ctx, "Unfounded", 0, "../..", ".", "/", NULL) != 0);
}


static void testTemp(MprTestGroup *gp)
{
    char    *path;

    path = mprGetTempPath(gp, NULL);
    assert(path && *path);
    assert(mprIsAbsPath(gp, path));
    assert(mprPathExists(gp, path, F_OK));
    mprDeletePath(gp, path);
    mprFree(path);
}


static void testTransform(MprTestGroup *gp)
{
    MprCtx  ctx;
    char    *path;

    ctx = mprAlloc(gp, 1);
    path = mprGetTransformedPath(ctx, "/", MPR_PATH_ABS);
    assert(mprIsAbsPath(ctx, path));

    path = mprGetTransformedPath(ctx, "/", MPR_PATH_REL);
    assert(mprIsRelPath(ctx, path));
    assert(path[0] == '.');

    path = mprGetTransformedPath(ctx, "/", 0);
    assert(strcmp(path, "/") == 0);

#if BLD_WIN_LIKE 
    path = mprGetTransformedPath(ctx, "/", MPR_PATH_ABS);
    mprAssert(mprIsAbsPath(ctx, path));

    /* Test MapSeparators */
    path = mprStrdup(ctx, "\\a\\b\\c\\d");
    mprMapSeparators(ctx, path, '/');
    assert(*path == '/');
    assert(strchr(path, '\\') == 0);

    /* Test PortablePath */
    path = mprStrdup(ctx, "\\a\\b\\c\\d");
    path = mprGetPortablePath(ctx, path);
    mprAssert(*path == '/');
    assert(strchr(path, '\\') == 0);
    assert(mprIsAbsPath(ctx, path));
#endif
    mprFree(ctx);
}

    
MprTestDef testPath = {
    "path", 0, initPath, termPath,
    {
        MPR_TEST(0, testCopyPath),
        MPR_TEST(0, testAbsPath),
        MPR_TEST(0, testJoinPath),
        MPR_TEST(0, testMakeDir),
        MPR_TEST(0, testNormalize),
        MPR_TEST(0, testRelPath),
        MPR_TEST(0, testSame),
        MPR_TEST(0, testSearch),
        MPR_TEST(0, testTransform),
        MPR_TEST(0, testTemp),
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
