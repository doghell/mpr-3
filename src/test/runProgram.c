/**
 *  runProgram.c - Test program for testCmd
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/************************************ Code ************************************/

MAIN(runProgramMain, int argc, char* argv[])
{
    Mpr     *mpr;
    MprFile *out;
    char    buf[256], *ep;
    int     i, len, exitCode, sofar;

    mpr = mprCreate(argc, argv, NULL);

#if TRACE_PROGRESS
    MprFile *f = mprOpen(mpr, "/tmp/r.log", O_CREAT|O_TRUNC|O_WRONLY, 0664);
    mprWriteFormat(f, "runProgram: argc %d\n", argc);
    for (i = 0; i < argc; i++) {
        mprWriteFormat(f, "runProgram: arg[%d] = %s\n", i, argv[i]);
    }
    mprFree(f);
#endif

    if (argc < 2) {
        mprPrintfError(mpr, "Usage: runProgram exitCode args...\n");
        exit(3);
    }
    exitCode = atoi(argv[1]);
    out = mprGetStdout(mpr);

    if (exitCode != 99) {
        /*
         *  Echo the args
         */
        for (i = 2; i < argc; ) {
            mprPuts(out, argv[i]);
            if (++i != argc) {
                mprPutc(out, ' ');
            }
        }
        mprPutc(out, '\n');

        /*
         *  Echo the CMD_ENV environment variable value
         */
        ep = getenv("CMD_ENV");
        if (ep) {
            mprPuts(out, "CMD_ENV=");
            mprPuts(out, ep);
        } else {
            mprPuts(out, "Can't find CMD_ENV");
        }
        mprPutc(out, '\n');
        mprFlush(out);
    }

    /*
     *  Read the input
     */
    sofar = 0;
    while ((len = read(0, buf, sizeof(buf))) > 0) {
        sofar += write(1, buf, len);
    }
    if (exitCode != 99) {
        mprPuts(out, "END");
        mprPutc(out, '\n');
    }
    mprFlush(out);
    return exitCode;
}

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
