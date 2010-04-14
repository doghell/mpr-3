/**
    mprPath.c - Path (filename) services.
 *
    This modules provides cross platform path name services.
 *
    Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/****************************** Forward Declarations **************************/
/*
    Find the first separator in the path
 */
#if BLD_UNIX_LIKE
    #define firstSep(fs, path)      strchr(path, fs->separators[0])
#else
    #define firstSep(fs, path)      strpbrk(path, fs->separators)
#endif

#define defaultSep(fs)              (fs->separators[0])

static MPR_INLINE bool isSep(MprFileSystem *fs, int c) 
{
    char    *separators;

    mprAssert(fs);

    for (separators = fs->separators; *separators; separators++) {
        if (*separators == c)
            return 1;
    }
    return 0;
}


static MPR_INLINE bool hasDrive(MprFileSystem *fs, cchar *path) 
{
    char    *cp, *endDrive;

    mprAssert(fs);
    mprAssert(path);

    if (fs->hasDriveSpecs) {
        cp = firstSep(fs, path);
        endDrive = strchr(path, ':');
        if (endDrive && (cp == NULL || endDrive < cp)) {
            return 1;
        }
    }
    return 0;
}


static MPR_INLINE bool isAbsPath(MprFileSystem *fs, cchar *path) 
{
    char    *cp, *endDrive;

    mprAssert(fs);
    mprAssert(path);

    if (fs->hasDriveSpecs) {
        if ((cp = firstSep(fs, path)) != 0) {
            if ((endDrive = strchr(path, ':')) != 0) {
                if (&endDrive[1] == cp) {
                    return 1;
                }
            }
            if (cp == path) {
                return 1;
            }
        }
    } else {
        if (isSep(fs, path[0])) {
            return 1;
        }
    }
    return 0;
}


static MPR_INLINE bool isFullPath(MprFileSystem *fs, cchar *path) 
{
    char    *cp, *endDrive;

    mprAssert(fs);
    mprAssert(path);

    if (fs->hasDriveSpecs) {
        cp = firstSep(fs, path);
        endDrive = strchr(path, ':');
        if (endDrive && cp && &endDrive[1] == cp) {
            return 1;
        }
    } else {
        if (isSep(fs, path[0])) {
            return 1;
        }
    }
    return 0;
}


static MPR_INLINE bool isRoot(MprFileSystem *fs, cchar *path) 
{
    char    *cp;

    if (isFullPath(fs, path)) {
        cp = firstSep(fs, path);
        if (cp && cp[1] == '\0') {
            return 1;
        }
    }
    return 0;
}


static MPR_INLINE char *lastSep(MprFileSystem *fs, cchar *path) 
{
    char    *cp;

    for (cp = (char*) &path[strlen(path)] - 1; cp >= path; cp--) {
        if (isSep(fs, *cp)) {
            return cp;
        }
    }
    return 0;
}

/************************************ Code ************************************/
/*
    This copies the filename at the designated path
 */
int mprCopyPath(MprCtx ctx, cchar *fromName, cchar *toName, int mode)
{
    MprFile     *from, *to;
    char        buf[MPR_BUFSIZE];
    int         count;

    if ((from = mprOpen(ctx, fromName, O_RDONLY | O_BINARY, 0)) == 0) {
        mprError(ctx, "Can't open %s", fromName);
        return MPR_ERR_CANT_OPEN;
    }
    if ((to = mprOpen(ctx, toName, O_WRONLY | O_TRUNC | O_CREAT | O_BINARY, mode)) == 0) {
        mprError(ctx, "Can't open %s", toName);
        return MPR_ERR_CANT_OPEN;
    }
    
    while ((count = mprRead(from, buf, sizeof(buf))) > 0) {
        mprWrite(to, buf, count);
    }
    mprFree(from);
    mprFree(to);
    return 0;
}


int mprDeletePath(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;

    if (path == NULL || *path == '\0') {
        return MPR_ERR_CANT_ACCESS;
    }
    fs = mprLookupFileSystem(ctx, path);
    if (!mprPathExists(ctx, path, F_OK)) {
        return 0;
    }
    return fs->deletePath(fs, path);
}


/*
    Return an absolute (normalized) path.
 */
char *mprGetAbsPath(MprCtx ctx, cchar *pathArg)
{
    MprFileSystem   *fs;
    char            *path;

    if (pathArg == 0 || *pathArg == '\0') {
        pathArg = ".";
    }

#if BLD_FEATURE_ROMFS
    return mprGetNormalizedPath(ctx, pathArg);
#endif

    fs = mprLookupFileSystem(ctx, pathArg);
    if (isFullPath(fs, pathArg)) {
        return mprGetNormalizedPath(ctx, pathArg);
    }

#if BLD_WIN_LIKE && !WINCE
{
    char    buf[MPR_MAX_PATH];
    GetFullPathName(pathArg, sizeof(buf) - 1, buf, NULL);
    buf[sizeof(buf) - 1] = '\0';
    path = mprGetNormalizedPath(ctx, buf);
}
#elif VXWORKS
{
    char    *dir;
    if (hasDrive(fs, pathArg)) {
        dir = mprGetCurrentPath(ctx);
        path = mprJoinPath(ctx, dir, &strchr(pathArg, ':')[1]);
        mprFree(dir);

    } else {
        if (isAbsPath(fs, pathArg)) {
            /*
             *  Path is absolute, but without a drive. Use the current drive.
             */
            dir = mprGetCurrentPath(ctx);
            path = mprJoinPath(ctx, dir, pathArg);
            mprFree(dir);
        } else {
            dir = mprGetCurrentPath(ctx);
            path = mprJoinPath(ctx, dir, pathArg);
            mprFree(dir);
        }
    }
}
#else
{
    char   *dir;
    dir = mprGetCurrentPath(ctx);
    path = mprJoinPath(ctx, dir, pathArg);
    mprFree(dir);
}
#endif
    return path;
}


/*
    This will return a fully qualified absolute path for the current working directory
 */
char *mprGetCurrentPath(MprCtx ctx)
{
    MprFileSystem   *fs;
    char            dir[MPR_MAX_PATH];

    fs = mprLookupFileSystem(ctx, dir);
    if (getcwd(dir, sizeof(dir)) == 0) {
        return mprGetAbsPath(ctx, "/");
    }

#if VXWORKS
{
    char    sep[2];

    /*
     *  Vx will sometimes just return a drive with no path.
     */
    if (firstSep(fs, dir) == NULL) {
        sep[0] = defaultSep(fs);
        sep[1] = '\0';
        return mprStrcat(ctx, -1, dir, sep, NULL);
    }
}
#elif BLD_WIN_LIKE
    mprMapSeparators(ctx, dir, fs->separators[0]);
#endif
    return mprStrdup(ctx, dir);
}


char *mprGetNativePath(MprCtx ctx, cchar *path)
{
    return mprGetTransformedPath(ctx, path, MPR_PATH_NATIVE_SEP);
}


/*
    Return the last portion of a pathname. The separators are not mapped and the path is not cleaned.
 */
char *mprGetPathBase(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;
    char            *cp;

    fs = mprLookupFileSystem(ctx, path);
    cp = (char*) lastSep(fs, path);
    if (cp == 0) {
        return mprStrdup(ctx, path);
    } 
    if (cp == path) {
        if (cp[1] == '\0') {
            return mprStrdup(ctx, path);
        }
    } else {
        if (cp[1] == '\0') {
            return mprStrdup(ctx, "");
        }
    }
    return mprStrdup(ctx, &cp[1]);
}


/*
    Return the directory portion of a pathname into the users buffer.
 */
char *mprGetPathDir(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;
    cchar           *cp;
    char            *result;
    int             len;

    mprAssert(path);
    mprAssert(ctx);

    if (*path == '\0') {
        return mprStrdup(ctx, path);
    }

    fs = mprLookupFileSystem(ctx, path);
    len = (int) strlen(path);
    cp = &path[len - 1];

    /*
     *  Step back over trailing slashes
     */
    while (cp > path && isSep(fs, *cp)) {
        cp--;
    }
    for (; cp > path && !isSep(fs, *cp); cp--) {
        ;
    }
    if (cp == path) {
        if (!isSep(fs, *cp)) {
            /* No slashes found, parent is current dir */
            return mprStrdup(ctx, ".");
        }
        return mprStrdup(ctx, fs->root);
    }

    len = (int) (cp - path);
    result = mprAlloc(ctx, len + 1);
    mprMemcpy(result, len + 1, path, len);
    result[len] = '\0';
    return result;
}


#if BLD_WIN_LIKE
MprList *mprGetPathFiles(MprCtx ctx, cchar *dir, bool enumDirs)
{
    HANDLE          h;
    MprDirEntry     *dp;
    MprPath         fileInfo;
    MprList         *list;
    char            *path, pbuf[MPR_MAX_PATH];
    int             sep;
#if WINCE
    WIN32_FIND_DATAA findData;
#else
    WIN32_FIND_DATA findData;
#endif

    list = 0;
    dp = 0;

    if ((path = mprJoinPath(ctx, dir, "*.*")) == 0) {
        return 0;
    }
    sep = mprGetPathSeparator(ctx, dir);

    h = FindFirstFile(path, &findData);
    if (h == INVALID_HANDLE_VALUE) {
        mprFree(path);
        return 0;
    }
    list = mprCreateList(ctx);

    do {
        if (findData.cFileName[0] == '.' && (findData.cFileName[1] == '\0' || findData.cFileName[1] == '.')) {
            continue;
        }
        if (enumDirs || !(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            dp = mprAllocObjZeroed(list, MprDirEntry);
            if (dp == 0) {
                mprFree(path);
                return 0;
            }

            dp->name = mprStrdup(dp, findData.cFileName);
            if (dp->name == 0) {
                mprFree(path);
                return 0;
            }

            /* dp->lastModified = (uint) findData.ftLastWriteTime.dwLowDateTime; */

            if (mprSprintf(pbuf, sizeof(pbuf), "%s%c%s", dir, sep, dp->name) < 0) {
                dp->lastModified = 0;
            } else {
                mprGetPathInfo(ctx, pbuf, &fileInfo);
                dp->lastModified = fileInfo.mtime;
            }
            dp->isDir = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;

#if FUTURE_64_BIT
            if (findData.nFileSizeLow < 0) {
                dp->size = (((uint64) findData.nFileSizeHigh) * INT64(4294967296)) + (4294967296L - 
                    (uint64) findData.nFileSizeLow);
            } else {
                dp->size = (((uint64) findData.nFileSizeHigh * INT64(4294967296))) +  (uint64) findData.nFileSizeLow;
            }
#else
            dp->size = (uint) findData.nFileSizeLow;
#endif
            mprAddItem(list, dp);
        }
    } while (FindNextFile(h, &findData) != 0);

    mprFree(path);
    FindClose(h);
    return list;
}
#endif /* WIN */


#if BLD_UNIX_LIKE || VXWORKS || CYGWIN
MprList *mprGetPathFiles(MprCtx ctx, cchar *path, bool enumDirs)
{
    DIR             *dir;
    MprPath         fileInfo;
    MprList         *list;
    struct dirent   *dirent;
    MprDirEntry     *dp;
    char            *fileName;
    int             rc;

    dp = 0;
    dir = opendir((char*) path);
    if (dir == 0) {
        return 0;
    }
    list = mprCreateList(ctx);

    while ((dirent = readdir(dir)) != 0) {
        if (dirent->d_name[0] == '.' && (dirent->d_name[1] == '\0' || dirent->d_name[1] == '.')) {
            continue;
        }
        fileName = mprJoinPath(ctx, path, dirent->d_name);
        rc = mprGetPathInfo(ctx, fileName, &fileInfo);
        mprFree(fileName);
        if (enumDirs || (rc == 0 && !fileInfo.isDir)) { 
            dp = mprAllocObjZeroed(list, MprDirEntry);
            if (dp == 0) {
                return 0;
            }
            dp->name = mprStrdup(dp, dirent->d_name);
            if (dp->name == 0) {
                return 0;
            }
            if (rc == 0) {
                dp->lastModified = fileInfo.mtime;
                dp->size = fileInfo.size;
                dp->isDir = fileInfo.isDir;
                dp->isLink = fileInfo.isLink;

            } else {
                dp->lastModified = 0;
                dp->size = 0;
                dp->isDir = 0;
                dp->isLink = 0;
            }
            mprAddItem(list, dp);
        }
    }
    closedir(dir);
    return list;
}
#endif


char *mprGetPathLink(MprCtx ctx, cchar *path)
{
    MprFileSystem  *fs;

    fs = mprLookupFileSystem(ctx, path);
    return fs->getPathLink(fs, path);
}


/*
    Return the extension portion of a pathname. Caller must not free the result.
 */
cchar *mprGetPathExtension(MprCtx ctx, cchar *path)
{
    MprFileSystem  *fs;
    char            *cp;

    if ((cp = strrchr(path, '.')) != NULL) {
        fs = mprLookupFileSystem(ctx, path);
        if (firstSep(fs, cp) == 0) {
            return ++cp;
        }
    } 
    return 0;
}


int mprGetPathInfo(MprCtx ctx, cchar *path, MprPath *info)
{
    MprFileSystem  *fs;

    fs = mprLookupFileSystem(ctx, path);
    return fs->getPathInfo(fs, path, info);
}


char *mprGetPathParent(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;
    char            *dir, *parent;

    fs = mprLookupFileSystem(ctx, path);

    if (path == 0 || path[0] == '\0') {
        return mprGetAbsPath(ctx, ".");
    }
    if (firstSep(fs, path) == NULL) {
        /*
         *  No parents in the path, so convert to absolute
         */
        dir = mprGetAbsPath(ctx, path);
        parent = mprGetPathDir(ctx, dir);
        mprFree(dir);
        return parent;
    }
    return mprGetPathDir(ctx, path);
}


char *mprGetPortablePath(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;
    char            *result, *cp;

    fs = mprLookupFileSystem(ctx, path);
    result = mprGetTransformedPath(ctx, path, 0);
    for (cp = result; *cp; cp++) {
        if (*cp == '\\') {
            *cp = '/';
        }
    }
    return result;
}


/*
    This returns a path relative to the current working directory for the given path
 */
char *mprGetRelPath(MprCtx ctx, cchar *pathArg)
{
    MprFileSystem   *fs;
    char            home[MPR_MAX_FNAME], *hp, *cp, *result, *tmp, *path, *mark;
    int             homeSegments, len, i, commonSegments, sep;

    fs = mprLookupFileSystem(ctx, pathArg);
    
    if (pathArg == 0 || *pathArg == '\0') {
        return mprStrdup(ctx, ".");
    }

    /*
     *  Must clean to ensure a minimal relative path result.
     */
    path = tmp = mprGetNormalizedPath(ctx, pathArg);

    if (!isAbsPath(fs, path)) {
        return path;
    }
    sep = (cp = firstSep(fs, path)) ? *cp : defaultSep(fs);
    
#if BLD_WIN_LIKE && !WINCE
{
    char    apath[MPR_MAX_FNAME];
    GetFullPathName(path, sizeof(apath) - 1, apath, NULL);
    apath[sizeof(apath) - 1] = '\0';
    path = apath;
    mprMapSeparators(fs, path, sep);
}
#endif
    /*
     *  Get the working directory. Ensure it is null terminated and leave room to append a trailing separators
     */
    if (getcwd(home, sizeof(home)) == 0) {
        strcpy(home, ".");
    }
    home[sizeof(home) - 2] = '\0';
    len = (int) strlen(home);

    /*
     *  Count segments in home working directory. Ignore trailing separators.
     */
    for (homeSegments = 0, cp = home; *cp; cp++) {
        if (isSep(fs, *cp) && cp[1]) {
            homeSegments++;
        }
    }

    /*
     *  Find portion of path that matches the home directory, if any. Start at -1 because matching root doesn't count.
     */
    commonSegments = -1;
    for (hp = home, mark = cp = path; *hp && *cp; hp++, cp++) {
        if (isSep(fs, *hp)) {
            if (isSep(fs, *cp)) {
                commonSegments++;
                mark = cp + 1;
            }
        } else if (fs->caseSensitive) {
            if (tolower((int) *hp) != tolower((int) *cp)) {
                break;
            }
        } else {
            if (*hp != *cp) {
                break;
            }
        }
    }
    mprAssert(commonSegments >= 0);

    /*
     *  Add one more segment if the last segment matches. Handle trailing separators
     */
    if ((isSep(fs, *hp) || *hp == '\0') && (isSep(fs, *cp) || *cp == '\0')) {
        commonSegments++;
        mark = cp;
    }
    
    if (isSep(fs, *cp)) {
        cp++;
    }
    
    hp = result = mprAlloc(ctx, homeSegments * 3 + (int) strlen(path) + 2);
    for (i = commonSegments; i < homeSegments; i++) {
        *hp++ = '.';
        *hp++ = '.';
        *hp++ = defaultSep(fs);
    }
    if (*cp) {
        strcpy(hp, cp);
    } else if (hp > result) {
        /*
         *  Cleanup trailing separators ("../" is the end of the new path)
         */
        hp[-1] = '\0';
    } else {
        strcpy(result, ".");
    }
    mprMapSeparators(fs, result, sep);
    mprFree(tmp);
    return result;
}


bool mprIsAbsPath(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;

    fs = mprLookupFileSystem(ctx, path);
    return isAbsPath(fs, path);
}


bool mprIsRelPath(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;

    fs = mprLookupFileSystem(ctx, path);
    return !isAbsPath(fs, path);
}


/*
    Join paths. Returns a joined (normalized) path.
    If other is absolute, then return other. If other is null, empty or "." then return path.
 */
char *mprJoinPath(MprCtx ctx, cchar *path, cchar *other)
{
    MprFileSystem   *fs;
    char            *join, *result, *drive, *cp;
    int             sep;

    fs = mprLookupFileSystem(ctx, path);
    if (other == NULL || *other == '\0' || strcmp(other, ".") == 0) {
        return mprStrdup(ctx, path);
    }
    if (isAbsPath(fs, other)) {
        if (fs->hasDriveSpecs && !isFullPath(fs, other) && isFullPath(fs, path)) {
            /*
             *  Other is absolute, but without a drive. Use the drive from path.
             */
            drive = mprStrdup(ctx, path);
            if ((cp = strchr(drive, ':')) != 0) {
                *++cp = '\0';
            }
            result = mprStrcat(ctx, -1, drive, other, NULL);
            mprFree(drive);
            return result;
        } else {
            return mprGetNormalizedPath(ctx, other);
        }
    }
    if (path == NULL || *path == '\0') {
        return mprGetNormalizedPath(ctx, other);
    }
    if ((cp = firstSep(fs, path)) != 0) {
        sep = *cp;
    } else if ((cp = firstSep(fs, other)) != 0) {
        sep = *cp;
    } else {
        sep = defaultSep(fs);
    }
    if ((join = mprAsprintf(ctx, -1, "%s%c%s", path, sep, other)) == 0) {
        return 0;
    }
    result = mprGetNormalizedPath(ctx, join);
    mprFree(join);
    return result;
}


/*
    Join an extension to a path. If path already has an extension, this call does nothing.
 */
char *mprJoinPathExt(MprCtx ctx, cchar *path, cchar *ext)
{
    MprFileSystem   *fs;
    char            *cp;

    fs = mprLookupFileSystem(ctx, path);
    if (ext == NULL || *ext == '\0') {
        return mprStrdup(ctx, path);
    }
    cp = strrchr(path, '.');
    if (cp && firstSep(fs, cp) == 0) {
        return mprStrdup(ctx, path);
    }
    return mprStrcat(ctx, -1, path, ext, NULL);
}


/*
    Make a directory with all necessary intervening directories.
 */
int mprMakeDir(MprCtx ctx, cchar *path, int perms, bool makeMissing)
{
    MprFileSystem   *fs;
    char            *parent;
    int             rc;

    fs = mprLookupFileSystem(ctx, path);

    if (mprPathExists(ctx, path, X_OK)) {
        return 0;
    }
    if (fs->makeDir(fs, path, perms) == 0) {
        return 0;
    }
    if (makeMissing && !isRoot(fs, path)) {
        parent = mprGetPathParent(ctx, path);
        rc = mprMakeDir(ctx, parent, perms, makeMissing);
        mprFree(parent);
        return fs->makeDir(fs, path, perms);
    }
    return MPR_ERR_CANT_CREATE;
}


int mprMakeLink(MprCtx ctx, cchar *path, cchar *target, bool hard)
{
    MprFileSystem   *fs;

    fs = mprLookupFileSystem(ctx, path);
    if (mprPathExists(ctx, path, X_OK)) {
        return 0;
    }
    return fs->makeLink(fs, path, target, hard);
}


char *mprGetTempPath(MprCtx ctx, cchar *tempDir)
{
    MprFileSystem   *fs;
    MprFile         *file;
    char            *dir, *path;
    int             i, now;
    static int      tempSeed = 0;

    fs = mprLookupFileSystem(ctx, tempDir ? tempDir : (cchar*) "/");

    if (tempDir == 0) {
#if WINCE
        dir = mprStrdup(ctx, "/Temp");
#elif BLD_WIN_LIKE
        dir = mprStrdup(ctx, getenv("TEMP"));
        mprMapSeparators(ctx, dir, defaultSep(fs));
#elif VXWORKS
        dir = mprStrdup(ctx, ".");
#else
        dir = mprStrdup(ctx, "/tmp");
#endif
    } else {
        dir = mprStrdup(ctx, tempDir);
    }

    now = ((int) mprGetTime(ctx) & 0xFFFF) % 64000;

    file = 0;
    path = 0;

    for (i = 0; i < 128; i++) {
        mprFree(path);
        path = mprAsprintf(ctx, -1, "%s/MPR_%d_%d_%d.tmp", dir, getpid(), now, ++tempSeed);
        file = mprOpen(ctx, path, O_CREAT | O_EXCL | O_BINARY, 0664);
        if (file) {
            mprFree(file);
            break;
        }
    }
    mprFree(dir);

    if (file == 0) {
        mprFree(path);
        return 0;
    }

    return path;
}


#if BLD_WIN_LIKE && FUTURE
/*
    Normalize to a cygwin path without a drive spec
 */
static char *toCygPath(MprCtx ctx, cchar *path)
{
    Mpr     *mpr;
    char    *absPath, *result;
    int     len;

    mpr = mprGetMpr(ctx);

    absPath = NULL;
    if (!isFullPath(mpr, path)) {
        absPath = mprGetAbsPath(path);
        path = (cchar*) absPath;
    }
    mprAssert(isFullPath(mpr, path);
        
    if (fs->cygdrive) {
        len = (int) strlen(fs->cygdrive);
        if (mprStrcmpAnyCaseCount(fs->cygdrive, &path[2], len) == 0 && isSep(mpr, path[len+2])) {
            /*
             *  If path is like: "c:/cygdrive/c/..."
             *  Just strip the "c:" portion. Still validly qualified.
             */
            result = mprStrdup(ctx, &path[len + 2]);

        } else {
            /*
             *  Path is like: "c:/some/other/path". Prepend "/cygdrive/c/"
             */
            result = mprAsprintf(ctx, -1, "%s/%c%s", fs->cygdrive, path[0], &path[2]);
            len = strlen(result);
            if (isSep(mpr, result[len-1])) {
                result[len-1] = '\0';
            }
        }
    } else {
        /*
         *  Best we can do is get a relative path
         */
        result = mprGetRelPath(ctx, pathArg);
    }
    mprFree(absPath);
    return result;
}


/*
    Convert from a cygwin path
 */
static char *fromCygPath(MprCtx ctx, cchar *path)
{
    Mpr     *mpr;
    char    *buf, *result;
    int     len;

    mpr = mprGetMpr(ctx);

    if (isFullPath(mpr, path)) {
        return mprStrdup(ctx, path);
    }
    if (fs->cygdrive) {
        len = (int) strlen(fs->cygdrive);
        if (mprComparePath(mpr, fs->cygdrive, path, len) == 0 && isSep(mpr, path[len]) && 
                isalpha(path[len+1]) && isSep(mpr, path[len+2])) {
            /*
             *  Has a "/cygdrive/c/" style prefix
             */
            buf = mprAsprintf(ctx, -1, "%c:", path[len+1], &path[len + 2]);

        } else {
            /*
             *  Cygwin path. Prepend "c:/cygdrive"
             */
            buf = mprAsprintf(ctx, -1, "%s/%s", fs->cygdrive, path);
        }
        result = mprGetAbsPath(ctx, buf);
        mprFree(buf);

    } else {
        result = mprGetAbsPath(ctx, path);
    }
    mprMapSeparators(mpr, result, defaultSep(fs));
    return result;
}
#endif


/*
    Normalize a path to remove redundant "./" and cleanup "../" and make separators uniform. Does not make an abs path.
    It does not map separators or make lower case.
 */
char *mprGetNormalizedPath(MprCtx ctx, cchar *pathArg)
{
    MprFileSystem   *fs;
    char            *dupPath, *path, *sp, *dp, *mark, **segments;
    int             addSep, i, segmentCount, hasDot, len, last, sep;

    if (pathArg == 0 || *pathArg == '\0') {
        return mprStrdup(ctx, ".");
    }

    fs = mprLookupFileSystem(ctx, pathArg);

    /*
     *  Allocate one spare byte incase we need to break into segments. If so, will add a trailing
     *  "/" to make parsing easier later.
     */
    len = strlen(pathArg);
    if ((path = mprAlloc(ctx, len + 2)) == 0) {
        return NULL;
    }
    dupPath = path;
    strcpy(path, pathArg);
    sep = (sp = firstSep(fs, path)) ? *sp : defaultSep(fs);

    /*
     *  Remove multiple path separators and map to the default separator.
     *  Check if we have any "." characters and count the number of path segments
     */
    hasDot = segmentCount = 0;
    for (sp = dp = mark = path; *sp; ) {
        if (isSep(fs, *sp)) {
            *sp = sep;
            segmentCount++;
            while (isSep(fs, sp[1])) {
                sp++;
            }
            mark = sp + 1;
        } 
        if (*sp == '.') {
            hasDot++;
        }
        *dp++ = *sp++;
    }
    *dp = '\0';
    if (!sep) {
        sep = defaultSep(fs);
    }

    if (!hasDot && segmentCount == 0) {
        if (fs->hasDriveSpecs) {
            last = path[strlen(path) - 1];
            if (last == ':') {
                path = mprStrcat(ctx, -1, path, ".", NULL);
                mprFree(dupPath);
            }
        }
        return path;
    }

    if (dp > path && !isSep(fs, dp[-1])) {
        *dp++ = sep;
        *dp = '\0';
        segmentCount++;
    }

    /*
     *  Have dots to process so break into path segments. Add one incase we need have an absolute path with a drive-spec.
     */
    mprAssert(segmentCount > 0);
    if ((segments = mprAlloc(ctx, sizeof(char*) * (segmentCount + 1))) == 0) {
        mprFree(dupPath);
        return NULL;
    }

    /*
     *  NOTE: The root "/" for absolute paths will be stored as empty.
     */
    i = len = 0;
    for (mark = sp = path; *sp; sp++) {
        if (isSep(fs, *sp)) {
            *sp = '\0';
            if (*mark == '.' && mark[1] == '\0' && segmentCount > 1) {
                /* Remove "."  However, preserve lone "." */
                mark = sp + 1;
                segmentCount--;
                continue;
            }
            if (*mark == '.' && mark[1] == '.' && mark[2] == '\0' && i > 0 && strcmp(segments[i-1], "..") != 0) {
                /* Erase ".." and previous segment */
                if (*segments[i - 1] == '\0' ) {
                    mprAssert(i == 1);
                    /* Previous segement is "/". Prevent escape from root */
                    segmentCount--;
                } else {
                    i--;
                    segmentCount -= 2;
                }
                mprAssert(segmentCount >= 0);
                mark = sp + 1;
                continue;
            }
            segments[i++] = mark;
            len += sp - mark;
#if KEEP
            if (i == 1 && segmentCount == 1 && fs->hasDriveSpecs && strchr(mark, ':') != 0) {
                /*
                 *  Normally we truncate a trailing "/", but this is an absolute path with a drive spec (c:/). 
                 */
                segments[i++] = "";
                segmentCount++;
            }
#endif
            mark = sp + 1;
        }
    }

    if (--sp > mark) {
        segments[i++] = mark;
        len += sp - mark;
    }
    mprAssert(i <= segmentCount);
    segmentCount = i;

    if (segmentCount <= 0) {
        mprFree(dupPath);
        mprFree(segments);
        return mprStrdup(ctx, ".");
    }

    addSep = 0;
    sp = segments[0];
    if (fs->hasDriveSpecs && *sp != '\0') {
        last = sp[strlen(sp) - 1];
        if (last == ':') {
            /* This matches an original path of: "c:/" but not "c:filename" */
            addSep++;
        }
    }
#if BLD_WIN_LIKE
    if (strcmp(segments[segmentCount - 1], " ") == 0) {
        segmentCount--;
    }
#endif

    if ((path = mprAlloc(ctx, len + segmentCount + 1)) == 0) {
        mprFree(segments);
        mprFree(dupPath);
        return NULL;
    }

    mprAssert(segmentCount > 0);

    /*
     *  First segment requires special treatment due to drive specs
     */
    dp = path;
    strcpy(dp, segments[0]);
    dp += strlen(segments[0]);

    if (segmentCount == 1 && (addSep || (*segments[0] == '\0'))) {
        *dp++ = sep;
    }
    for (i = 1; i < segmentCount; i++) {
        *dp++ = sep;
        strcpy(dp, segments[i]);
        dp += strlen(segments[i]);
    }
    *dp = '\0';
    mprFree(dupPath);
    mprFree(segments);
    return path;
}


int mprGetPathSeparator(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;

    fs = mprLookupFileSystem(ctx, path);
    return fs->separators[0];
}


bool mprIsPathSeparator(MprCtx ctx, cchar *path, cchar c)
{
    MprFileSystem   *fs;

    fs = mprLookupFileSystem(ctx, path);
    return isSep(fs, c);
}


/*
    Return a pointer into the path at the last path separator or null if none found
 */
cchar *mprGetLastPathSeparator(MprCtx ctx, cchar *path) 
{
    MprFileSystem   *fs;

    fs = mprLookupFileSystem(ctx, path);
    return lastSep(fs, path);
}


cchar *mprGetFirstPathSeparator(MprCtx ctx, cchar *path) 
{
    MprFileSystem   *fs;

    fs = mprLookupFileSystem(ctx, path);
    return firstSep(fs, path);
}


void mprMapSeparators(MprCtx ctx, char *path, int separator)
{
    MprFileSystem   *fs;
    char            *cp;

    fs = mprLookupFileSystem(ctx, path);
    for (cp = path; *cp; cp++) {
        if (isSep(fs, *cp)) {
            *cp = separator;
        }
    }
}


bool mprPathExists(MprCtx ctx, cchar *path, int omode)
{
    MprFileSystem  *fs;

    fs = mprLookupFileSystem(ctx, path);
    return fs->accessPath(fs, path, omode);
}


/*
    Resolve one path against another path. Returns a joined (normalized) path.
    If other is absolute, then return other. If other is null, empty or "." then return path.
 */
char *mprResolvePath(MprCtx ctx, cchar *path, cchar *other)
{
    MprFileSystem   *fs;
    char            *join, *result, *drive, *cp, *dir;

    fs = mprLookupFileSystem(ctx, path);
    if (other == NULL || *other == '\0' || strcmp(other, ".") == 0) {
        return mprStrdup(ctx, path);
    }
    if (isAbsPath(fs, other)) {
        if (fs->hasDriveSpecs && !isFullPath(fs, other) && isFullPath(fs, path)) {
            /*
                Other is absolute, but without a drive. Use the drive from path.
             */
            drive = mprStrdup(ctx, path);
            if ((cp = strchr(drive, ':')) != 0) {
                *++cp = '\0';
            }
            result = mprStrcat(ctx, -1, drive, other, NULL);
            mprFree(drive);
            return result;
        }
        return mprGetNormalizedPath(ctx, other);
    }
    if (path == NULL || *path == '\0') {
        return mprGetNormalizedPath(ctx, other);
    }
    dir = mprGetPathDir(ctx, path);
    if ((join = mprAsprintf(ctx, -1, "%s/%s", dir, other)) == 0) {
        mprFree(dir);
        return 0;
    }
    mprFree(dir);
    result = mprGetNormalizedPath(ctx, join);
    mprFree(join);
    return result;
}


/*
    Compare two file path to determine if they point to the same file.
 */
int mprSamePath(MprCtx ctx, cchar *path1, cchar *path2)
{
    MprFileSystem   *fs;
    cchar           *p1, *p2;
    int             rc;

    fs = mprLookupFileSystem(ctx, path1);

    /*
        Convert to absolute (normalized) paths to compare. 
     */
    if (!isFullPath(fs, path1)) {
        path1 = mprGetAbsPath(ctx, path1);
    } else {
        path1 = mprGetNormalizedPath(ctx, path1);
    }
    if (!isFullPath(fs, path2)) {
        path2 = mprGetAbsPath(ctx, path2);
    } else {
        path2 = mprGetNormalizedPath(ctx, path2);
    }
    if (fs->caseSensitive) {
        for (p1 = path1, p2 = path2; *p1 && *p2; p1++, p2++) {
            if (*p1 != *p2 && !(isSep(fs, *p1) && isSep(fs, *p2))) {
                break;
            }
        }
    } else {
        for (p1 = path1, p2 = path2; *p1 && *p2; p1++, p2++) {
            if (tolower((int) *p1) != tolower((int) *p2) && !(isSep(fs, *p1) && isSep(fs, *p2))) {
                break;
            }
        }
    }
    rc = (*p1 == *p2);
    mprFree((char*) path1);
    mprFree((char*) path2);
    return rc;
}


/*
    Compare two file path to determine if they point to the same file.
 */
int mprSamePathCount(MprCtx ctx, cchar *path1, cchar *path2, int len)
{
    MprFileSystem   *fs;
    char            *tmpPath1, *tmpPath2;
    cchar           *p1, *p2;

    fs = mprLookupFileSystem(ctx, path1);
    tmpPath1 = tmpPath2 = 0;

    /*
        Convert to absolute paths to compare. 
     */
    if (!isFullPath(fs, path1)) {
        tmpPath1 = mprGetAbsPath(ctx, path1);
        path1 = tmpPath1;
    }
    if (!isFullPath(fs, path2)) {
        tmpPath2 = mprGetAbsPath(ctx, path2);
        path2 = tmpPath2;
    }
    if (fs->caseSensitive) {
        for (p1 = path1, p2 = path2; *p1 && *p2 && len > 0; p1++, p2++, len--) {
            if (*p1 != *p2 && !(isSep(fs, *p1) && isSep(fs, *p2))) {
                break;
            }
        }
    } else {
        for (p1 = path1, p2 = path2; *p1 && *p2 && len > 0; p1++, p2++, len--) {
            if (tolower((int) *p1) != tolower((int) *p2) && !(isSep(fs, *p1) && isSep(fs, *p2))) {
                break;
            }
        }
    }
    mprFree(tmpPath1);
    mprFree(tmpPath2);
    return len == 0;
}




char *mprSearchPath(MprCtx ctx, cchar *file, int flags, cchar *search, ...)
{
    va_list     args;
    char        *path, *tok, *dir, *result, *nextDir;
    int         access;

    va_start(args, search);
    access = (flags & MPR_SEARCH_EXE) ? X_OK : R_OK;

    for (nextDir = (char*) search; nextDir; nextDir = va_arg(args, char*)) {

        if (strchr(nextDir, MPR_SEARCH_SEP_CHAR)) {
            tok = NULL;
            nextDir = mprStrdup(ctx, nextDir);
            dir = mprStrTok(nextDir, MPR_SEARCH_SEP, &tok);
            while (dir && *dir) {
                mprLog(ctx, 5, "mprSearchForFile: %s in directory %s", file, nextDir);
                path = mprJoinPath(ctx, dir, file);
                if (mprPathExists(ctx, path, R_OK)) {
                    mprLog(ctx, 5, "mprSearchForFile: found %s", path);
                    result = mprGetNormalizedPath(ctx, path);
                    mprFree(path);
                    mprFree(nextDir);
                    return result;
                }
                mprFree(path);
                dir = mprStrTok(0, MPR_SEARCH_SEP, &tok);
            }
            mprFree(nextDir);

        } else {
            mprLog(ctx, 5, "mprSearchForFile: %s in directory %s", file, nextDir);
            path = mprJoinPath(ctx, nextDir, file);
            if (mprPathExists(ctx, path, R_OK)) {
                mprLog(ctx, 5, "mprSearchForFile: found %s", path);
                result = mprGetNormalizedPath(ctx, path);
                mprFree(path);
                return result;
            }
        }
    }
    va_end(args);
    return 0;
}


/*
    This normalizes a path. Returns a normalized path according to flags. Default is absolute. 
    This will map separators to the current representation unless MPR_PATH_NATIVE_SEP is specified in the flags.
 */
char *mprGetTransformedPath(MprCtx ctx, cchar *path, int flags)
{
    MprFileSystem       *fs;
    char                *result;

    fs = mprLookupFileSystem(ctx, path);

#if BLD_WIN_LIKE && FUTURE
    if (flags & MPR_PATH_CYGWIN) {
        result = toCygPath(ctx, path, flags);
    } else {
        /*
         *  Issues here. "/" is ambiguous. Is this "c:/" or is it "c:/cygdrive/c" which may map to c:/cygwin/...
         */
        result = fromCygPath(ctx, path);
    }
#endif

    if (flags & MPR_PATH_ABS) {
        result = mprGetAbsPath(ctx, path);

    } else if (flags & MPR_PATH_REL) {
        result = mprGetRelPath(ctx, path);

    } else {
        result = mprGetNormalizedPath(ctx, path);
    }

#if BLD_WIN_LIKE
    if (flags & MPR_PATH_NATIVE_SEP) {
        mprMapSeparators(ctx, result, '\\');
    }
#endif
    return result;
}


/*
    Return the extension portion of a pathname. Caller must not free the result.
 */
char *mprTrimPathExtension(MprCtx ctx, cchar *path)
{
    MprFileSystem   *fs;
    char            *cp, *ext;

    fs = mprLookupFileSystem(ctx, path);
    ext = mprStrdup(ctx, path);
    if ((cp = strrchr(ext, '.')) != NULL) {
        if (firstSep(fs, cp) == 0) {
            *cp = '\0';
        }
    } 
    return ext;
}


int mprTruncatePath(MprCtx ctx, cchar *path, int size)
{
    if (!mprPathExists(ctx, path, F_OK)) {
        return MPR_ERR_CANT_ACCESS;
    }
#if BLD_WIN_LIKE
{
    HANDLE  h;

    h = CreateFile(path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    SetFilePointer(h, size, 0, FILE_BEGIN);
    if (h == INVALID_HANDLE_VALUE || SetEndOfFile(h) == 0) {
        CloseHandle(h);
        return MPR_ERR_CANT_WRITE;
    }
    CloseHandle(h);
}
#elif VXWORKS
{
#if FUTURE
    int     fd;

    fd = open(path, O_WRONLY, 0664);
    if (fd < 0 || ftruncate(fd, size) < 0) {
        return MPR_ERR_CANT_WRITE;
    }
    close(fd);
#endif
    return MPR_ERR_CANT_WRITE;
}
#else
    if (truncate(path, size) < 0) {
        return MPR_ERR_CANT_WRITE;
    }
#endif
    return 0;
}


char *mprGetAppPath(MprCtx ctx)
{ 
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    if (mpr->appPath) {
        return mprStrdup(ctx, mpr->appPath);
    }

#if MACOSX
    char    path[MPR_MAX_PATH], pbuf[MPR_MAX_PATH];
    uint    size;
    int     len;

    size = sizeof(path) - 1;
    if (_NSGetExecutablePath(path, &size) < 0) {
        return mprGetAbsPath(ctx, ".");
    }
    path[size] = '\0';
    len = readlink(path, pbuf, sizeof(pbuf) - 1);
    if (len < 0) {
        return mprGetAbsPath(ctx, path);
    }
    pbuf[len] = '\0';
    mpr->appPath = mprGetAbsPath(ctx, pbuf);
    return mprStrdup(ctx, mpr->appPath);

#elif BLD_UNIX_LIKE 
    char    pbuf[MPR_MAX_STRING], *path;
    int     len;

    path = mprAsprintf(ctx, -1, "/proc/%i/exe", getpid()); 

    len = readlink(path, pbuf, sizeof(pbuf) - 1);
    if (len < 0) {
        mprFree(path);
        return mprGetAbsPath(ctx, ".");
    }
    pbuf[len] = '\0';
    mprFree(path);
    mpr->appPath = mprGetAbsPath(ctx, pbuf); 
    return mprStrdup(ctx, mpr->appPath);

#elif BLD_WIN_LIKE
{
    char    path[MPR_MAX_PATH];

    if (GetModuleFileName(0, path, sizeof(path) - 1) <= 0) {
        return 0;
    }
    mpr->appPath = mprGetAbsPath(ctx, path);
    return mprStrdup(ctx, mpr->appPath);
}
#else
    mpr->appPath = mprGetCurrentPath(ctx);
    return mprStrdup(ctx, mpr->appPath);
#endif
}

 
char *mprGetAppDir(MprCtx ctx)
{ 
    Mpr     *mpr;
    char    *path;

    mpr = mprGetMpr(ctx);
    if (mpr->appDir == 0) {
        path = mprStrdup(ctx, mprGetAppPath(ctx));
        mpr->appDir = mprGetPathDir(mpr, path);
        mprFree(path);
    }
    return mprStrdup(ctx, mpr->appDir); 
} 

/*
    @copy   default
    
    Copyright (c) Embedthis Software LLC, 2003-2010. All Rights Reserved.
    Copyright (c) Michael O'Brien, 1993-2010. All Rights Reserved.
    
    This software is distributed under commercial and open source licenses.
    You may use the GPL open source license described below or you may acquire 
    a commercial license from Embedthis Software. You agree to be fully bound 
    by the terms of either license. Consult the LICENSE.TXT distributed with 
    this software for full details.
    
    This software is open source; you can redistribute it and/or modify it 
    under the terms of the GNU General Public License as published by the 
    Free Software Foundation; either version 2 of the License, or (at your 
    option) any later version. See the GNU General Public License for more 
    details at: http://www.embedthis.com/downloads/gplLicense.html
    
    This program is distributed WITHOUT ANY WARRANTY; without even the 
    implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. 
    
    This GPL license does NOT permit incorporating this software into 
    proprietary programs. If you are unable to comply with the GPL, you must
    acquire a commercial license to use this software. Commercial licenses 
    for this software and support services are available from Embedthis 
    Software at http://www.embedthis.com 
    
    @end
 */
