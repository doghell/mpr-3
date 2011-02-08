/**
 *  mprWin.c - Windows specific adaptions
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

#if BLD_WIN_LIKE && !WINCE
/**************************** Forward Declarations ****************************/

static cchar    *getHive(cchar *key, HKEY *root);

/*********************************** Code *************************************/
/*
 *  Initialize the O/S platform layer
 */ 

MprOsService *mprCreateOsService(MprCtx ctx)
{
    return mprAllocObj(ctx, MprOsService);
}


int mprStartOsService(MprOsService *os)
{
    WSADATA     wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        return -1;
    }
    return 0;
}


void mprStopOsService(MprOsService *os)
{
    WSACleanup();
}


long mprGetInst(Mpr *mpr)
{
    return (long) mpr->appInstance;
}


HWND mprGetHwnd(MprCtx ctx)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    return mpr->waitService->hwnd;
}


int mprGetRandomBytes(MprCtx ctx, char *buf, int length, int block)
{
    HCRYPTPROV      prov;
    int             rc;

    rc = 0;

    if (!CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | 0x40)) {
        return mprGetError();
    }
    if (!CryptGenRandom(prov, length, buf)) {
        rc = mprGetError();
    }
    CryptReleaseContext(prov, 0);
    return rc;
}


MprModule *mprLoadModule(MprCtx ctx, cchar *moduleName, cchar *initFunction)
{
    MprModule       *mp;
    MprModuleEntry  fn;
    char            *module;
    char            *path, *name;
    void            *handle;

    mprAssert(moduleName && *moduleName);

    mp = 0;
    name = path = 0;
    module = mprGetNormalizedPath(ctx, moduleName);

    if (mprSearchForModule(ctx, module, &path) < 0) {
        mprError(ctx, "Can't find module \"%s\" in search path \"%s\"", moduleName, mprGetModuleSearchPath(ctx));

    } else {
        name = mprGetPathBase(ctx, module);
        mprLog(ctx, MPR_INFO, "Loading module %s from %s", name, path);

        if ((handle = GetModuleHandle(name)) == 0 && (handle = LoadLibrary(path)) == 0) {
            mprError(ctx, "Can't load module %s\nReason: \"%d\"\n",  path, mprGetOsError());

        } else if (initFunction) {
            if ((fn = (MprModuleEntry) GetProcAddress((HINSTANCE) handle, initFunction)) != 0) {
                if ((mp = (fn)(ctx, path)) == 0) {
                    mprError(ctx, "Initialization for module %s failed", name);
                    FreeLibrary((HINSTANCE) handle);

                } else {
                    mp->handle = handle;
                }

            } else {
                mprError(ctx, "Can't load module %s\nReason: can't find function \"%s\"\n",  name, initFunction);
                FreeLibrary((HINSTANCE) handle);

            }
        }
    }
    mprFree(name);
    mprFree(path);
    mprFree(module);
    return mp;
}


int mprReadRegistry(MprCtx ctx, char **buf, int max, cchar *key, cchar *name)
{
    HKEY        top, h;
    char        *value;
    ulong       type, size;

    mprAssert(key && *key);
    mprAssert(buf);

    /*
     *  Get the registry hive
     */
    if ((key = getHive(key, &top)) == 0) {
        return MPR_ERR_CANT_ACCESS;
    }

    if (RegOpenKeyEx(top, key, 0, KEY_READ, &h) != ERROR_SUCCESS) {
        return MPR_ERR_CANT_ACCESS;
    }

    /*
     *  Get the type
     */
    if (RegQueryValueEx(h, name, 0, &type, 0, &size) != ERROR_SUCCESS) {
        RegCloseKey(h);
        return MPR_ERR_CANT_READ;
    }
    if (type != REG_SZ && type != REG_EXPAND_SZ) {
        RegCloseKey(h);
        return MPR_ERR_BAD_TYPE;
    }

    value = (char*) mprAlloc(ctx, size);
    if ((int) size > max) {
        RegCloseKey(h);
        return MPR_ERR_WONT_FIT;
    }
    if (RegQueryValueEx(h, name, 0, &type, (uchar*) value, &size) != ERROR_SUCCESS) {
        mprFree(value);
        RegCloseKey(h);
        return MPR_ERR_CANT_READ;
    }

    RegCloseKey(h);
    *buf = value;
    return 0;
}


void mprSetInst(Mpr *mpr, long inst)
{
    mpr->appInstance = inst;
}


void mprSetHwnd(MprCtx ctx, HWND h)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    mpr->waitService->hwnd = h;
}


void mprSetSocketMessage(MprCtx ctx, int socketMessage)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    mpr->waitService->socketMessage = socketMessage;
}


void mprSleep(MprCtx ctx, int milliseconds)
{
    Sleep(milliseconds);
}


uni *mprToUni(MprCtx ctx, cchar* a)
{
    uni     *wstr;
    int     len;

    len = MultiByteToWideChar(CP_ACP, 0, a, -1, NULL, 0);
    wstr = (uni*) mprAlloc(ctx, (len+1) * sizeof(uni));
    if (wstr) {
        MultiByteToWideChar(CP_ACP, 0, a, -1, wstr, len);
    }
    return wstr;
}


char *mprToAsc(MprCtx ctx, cuni *w)
{
    char    *str;
    int     len;

    len = WideCharToMultiByte(CP_ACP, 0, w, -1, NULL, 0, NULL, NULL);
    if ((str = mprAlloc(ctx, len + 1)) != 0) {
        WideCharToMultiByte(CP_ACP, 0, w, -1, str, (DWORD) len, NULL, NULL);
    }
    return str;
}


void mprUnloadModule(MprModule *mp)
{
    mprAssert(mp->handle);

    mprStopModule(mp);
    mprRemoveItem(mprGetMpr(mp)->moduleService->modules, mp);
    if (mp->handle) {
        FreeLibrary((HINSTANCE) mp->handle);
        mp->handle = 0;
    }
}


void mprWriteToOsLog(MprCtx ctx, cchar *message, int flags, int level)
{
    HKEY        hkey;
    void        *event;
    long        errorType;
    ulong       exists;
    char        buf[MPR_MAX_STRING], logName[MPR_MAX_STRING], *lines[9], *cp, *value;
    int         type;
    static int  once = 0;

    mprStrcpy(buf, sizeof(buf), message);
    cp = &buf[strlen(buf) - 1];
    while (*cp == '\n' && cp > buf) {
        *cp-- = '\0';
    }

    type = EVENTLOG_ERROR_TYPE;

    lines[0] = buf;
    lines[1] = 0;
    lines[2] = lines[3] = lines[4] = lines[5] = 0;
    lines[6] = lines[7] = lines[8] = 0;

    if (once == 0) {
        /*  Initialize the registry */
        once = 1;
        mprSprintf(logName, sizeof(logName), "SYSTEM\\CurrentControlSet\\Services\\EventLog\\Application\\%s",
            mprGetAppName(ctx));
        hkey = 0;

        if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, logName, 0, NULL, 0, KEY_ALL_ACCESS, NULL, &hkey, &exists) == ERROR_SUCCESS) {
            value = "%SystemRoot%\\System32\\netmsg.dll";
            if (RegSetValueEx(hkey, "EventMessageFile", 0, REG_EXPAND_SZ, 
                    (uchar*) value, (int) strlen(value) + 1) != ERROR_SUCCESS) {
                RegCloseKey(hkey);
                return;
            }

            errorType = EVENTLOG_ERROR_TYPE | EVENTLOG_WARNING_TYPE | EVENTLOG_INFORMATION_TYPE;
            if (RegSetValueEx(hkey, "TypesSupported", 0, REG_DWORD, (uchar*) &errorType, sizeof(DWORD)) != ERROR_SUCCESS) {
                RegCloseKey(hkey);
                return;
            }
            RegCloseKey(hkey);
        }
    }

    event = RegisterEventSource(0, mprGetAppName(ctx));
    if (event) {
        /*
         *  3299 is the event number for the generic message in netmsg.dll.
         *  "%1 %2 %3 %4 %5 %6 %7 %8 %9" -- thanks Apache for the tip
         */
        ReportEvent(event, EVENTLOG_ERROR_TYPE, 0, 3299, NULL, sizeof(lines) / sizeof(char*), 0, (LPCSTR*) lines, 0);
        DeregisterEventSource(event);
    }
}


int mprWriteRegistry(MprCtx ctx, cchar *key, cchar *name, cchar *value)
{
    HKEY    top, h, subHandle;
    ulong   disposition;

    mprAssert(key && *key);
    mprAssert(name && *name);
    mprAssert(value && *value);

    /*
     *  Get the registry hive
     */
    if ((key = getHive(key, &top)) == 0) {
        return MPR_ERR_CANT_ACCESS;
    }

    if (name) {
        /*
         *  Write a registry string value
         */
        if (RegOpenKeyEx(top, key, 0, KEY_ALL_ACCESS, &h) != ERROR_SUCCESS) {
            return MPR_ERR_CANT_ACCESS;
        }
        if (RegSetValueEx(h, name, 0, REG_SZ, value, (int) strlen(value) + 1) != ERROR_SUCCESS) {
            RegCloseKey(h);
            return MPR_ERR_CANT_READ;
        }

    } else {
        /*
         *  Create a new sub key
         */
        if (RegOpenKeyEx(top, key, 0, KEY_CREATE_SUB_KEY, &h) != ERROR_SUCCESS){
            return MPR_ERR_CANT_ACCESS;
        }
        if (RegCreateKeyEx(h, name, 0, NULL, REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, NULL, &subHandle, &disposition) != ERROR_SUCCESS) {
            return MPR_ERR_CANT_ACCESS;
        }
        RegCloseKey(subHandle);
    }
    RegCloseKey(h);
    return 0;
}


/*
 *  Determine the registry hive by the first portion of the path. Return 
 *  a pointer to the rest of key path after the hive portion.
 */ 
static cchar *getHive(cchar *keyPath, HKEY *hive)
{
    char    key[MPR_MAX_STRING], *cp;
    int     len;

    mprAssert(keyPath && *keyPath);

    *hive = 0;

    mprStrcpy(key, sizeof(key), keyPath);
    key[sizeof(key) - 1] = '\0';

    if (cp = strchr(key, '\\')) {
        *cp++ = '\0';
    }
    if (cp == 0 || *cp == '\0') {
        return 0;
    }

    if (!mprStrcmpAnyCase(key, "HKEY_LOCAL_MACHINE")) {
        *hive = HKEY_LOCAL_MACHINE;
    } else if (!mprStrcmpAnyCase(key, "HKEY_CURRENT_USER")) {
        *hive = HKEY_CURRENT_USER;
    } else if (!mprStrcmpAnyCase(key, "HKEY_USERS")) {
        *hive = HKEY_USERS;
    } else if (!mprStrcmpAnyCase(key, "HKEY_CLASSES_ROOT")) {
        *hive = HKEY_CLASSES_ROOT;
    } else {
        return 0;
    }

    if (*hive == 0) {
        return 0;
    }
    len = (int) strlen(key) + 1;
    return keyPath + len;
}

#else
void __dummyMprWin() {}
#endif /* BLD_WIN_LIKE */

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
