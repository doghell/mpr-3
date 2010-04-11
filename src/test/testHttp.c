/**
 *  testHttp.c - tests for HTTP
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

/************************************ Code ************************************/
#if BLD_FEATURE_HTTP_CLIENT

static int initHttp(MprTestGroup *gp)
{
    MprSocket   *sp;
    
    if (getenv("NO_INTERNET")) {
        gp->skip = 1;
        return 0;
    }
    sp = mprCreateSocket(gp, NULL);
    
    /*
     *  Test if we have network connectivity. If not, then skip these tests.
     */
    if (mprOpenClientSocket(sp, "embedthis.com", 80, 0) < 0) {
        static int once = 0;
        if (once++ == 0) {
            mprPrintf(gp, "%12s Disabling tests %s.*: no internet connection. %d\n", "[Notice]", gp->fullName, once);
        }
        gp->skip = 1;
    }
    mprFree(sp);
    
    return 0;
}


static void testCreateHttp(MprTestGroup *gp)
{
    MprHttp     *http;

    http = mprCreateHttp(gp);
    assert(http != 0);
    mprFree(http);
}


static void testBasicHttpGet(MprTestGroup *gp)
{
    MprHttp     *http;
    int         rc, code, length;

    http = mprCreateHttp(gp);
    assert(http != 0);

    rc = mprHttpRequest(http, "GET", "http://embedthis.com/index.html");
    assert(rc >= 0);
    if (rc >= 0) {
        code = mprGetHttpCode(http);
        assert(rc == code);
        assert(code == 200 || code == 302);
        if (code != 200 && code != 302) {
            mprLog(gp, 0, "HTTP response code %d", code);
        }
        assert(mprGetHttpError(http) != 0);
        length = mprGetHttpContentLength(http);
        assert(length != 0);
    }
    mprFree(http);
}


#if BLD_FEATURE_SSL && (BLD_FEATURE_MATRIXSSL || BLD_FEATURE_OPENSSL)
static void testSecureHttpGet(MprTestGroup *gp)
{
    MprHttp     *http;
    int         rc, code;

    http = mprCreateHttp(gp);
    assert(http != 0);

    rc = mprHttpRequest(http, "GET", "https://www.amazon.com/index.html");
    assert(rc >= 0);
    if (rc >= 0) {
        code = mprGetHttpCode(http);
        assert(rc == code);
        assert(code == 200 || code == 302);
        if (code != 200 && code != 302) {
            mprLog(gp, 0, "HTTP response code %d", code);
        }
        assert(mprGetHttpError(http) != 0);
    }
    mprFree(http);
}
#endif


MprTestDef testHttp = {
    "http", 0, initHttp, 0,
    {
        MPR_TEST(0, testCreateHttp),
        MPR_TEST(0, testBasicHttpGet),
#if BLD_FEATURE_SSL && (BLD_FEATURE_MATRIXSSL || BLD_FEATURE_OPENSSL)
        MPR_TEST(0, testSecureHttpGet),
#endif
        MPR_TEST(0, 0),
    },
};

#else /* BLD_FEATURE_HTTP_CLIENT */
void __dummy_testHttp() {}
#endif /* BLD_FEATURE_HTTP_CLIENT */


/*
 *  @copy   default
 *  
 *  Copyright (c) Embedthis Software LLC, 2003-2010. All Rights Reserved.
 *  Copyright (c) Michael O'Brien, 1993-2010. All Rights Reserved.
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
 *  @end
 */
