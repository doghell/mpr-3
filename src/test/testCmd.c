/**
 *  testCmd.c - Unit tests for Cmd
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mprTest.h"

#if BLD_FEATURE_CMD
/************************************ Code ************************************/

static void testCreateCmd(MprTestGroup *gp)
{
    MprCmd      *cmd;

    cmd = mprCreateCmd(gp);
    assert(cmd != 0);
    mprFree(cmd);
}


static void testRunCmd(MprTestGroup *gp)
{
    MprCmd      *cmd;
    char        *result, *command;
    int         rc, status, exitStatus;

    cmd = mprCreateCmd(gp);
    assert(cmd != 0);

    /*
     *  runProgram reads from the input, so it requires stdin to be connected
     */
    command = mprAsprintf(gp, -1, "%s/runProgram%s 0", mprGetAppDir(gp), BLD_EXE);
    status = mprRunCmd(cmd, command, &result, NULL, MPR_CMD_IN);
    assert(result != NULL);
    assert(status == 0);

    rc = mprGetCmdExitStatus(cmd, &exitStatus);
    assert(rc == 0);
    assert(exitStatus == 0);
    mprFree(cmd);
}


static int withDataCallback(MprCmd *cmd, int channel, void *data)
{
    MprTestGroup    *gp;
    MprBuf          *buf;
    int             space, len;

    gp = data;
    buf = (MprBuf*) gp->data;
    assert(buf != NULL);
    
    if (channel == MPR_CMD_STDIN) {
        ;
    } else if (channel == MPR_CMD_STDERR) {
        mprCloseCmdFd(cmd, channel);
        return 0;
    
    } else {    
        space = mprGetBufSpace(buf);
        if (space < (MPR_BUFSIZE / 4)) {
            if (mprGrowBuf(buf, MPR_BUFSIZE) < 0) {
                mprAssert(0);
                mprCloseCmdFd(cmd, channel);
                return 0;
            }
            space = mprGetBufSpace(buf);
        }
        len = mprReadCmdPipe(cmd, channel, mprGetBufEnd(buf), space);            
        if (len <= 0) {
            int status = mprGetError();
            mprLog(cmd, 5, "Read %d (errno %d) from %s", len, status, (channel == MPR_CMD_STDOUT) ? "stdout" : "stderr");
            if (len == 0 || (len < 0 && !(status == EAGAIN || status == EWOULDBLOCK))) {
                mprCloseCmdFd(cmd, channel);
#if UNUSED
                if (channel == MPR_CMD_STDOUT && cmd->flags & MPR_CMD_ERR) {
                    /*
                     *  Now that stdout is complete, enable stderr to receive an EOF or any error output.
                     *  This is serialized to eliminate both stdin and stdout events on different threads at the same time.
                     */
                    mprEnableCmdEvents(cmd, MPR_CMD_STDERR);
                }
            } else {
                mprEnableCmdEvents(cmd, channel);
#endif
            }
            return 0;
            
        } else {
            mprAdjustBufEnd(buf, len);
            mprAddNullToBuf(buf);
#if UNUSED
            mprEnableCmdEvents(cmd, channel);
#endif
        }
    }
    return 0;
}


/*
 *  Match s2 as a prefix of s1
 */
static int match(char *s1, char *s2)
{
    int     rc;

    if (s1 == 0) {
        return 0;
    }
    rc = strncmp(s1, s2, strlen(s2));
    return rc;
}


static void testWithData(MprTestGroup *gp)
{
    MprCmd      *cmd;
    MprBuf      *buf;
    char        *data, *env[16], *argv[16], line[80], *s, *tok;
    int         argc, i, status, rc, fd, len;

    cmd = mprCreateCmd(gp);
    assert(cmd != 0);

    gp->data = mprCreateBuf(gp, MPR_BUFSIZE, -1);
    assert(gp != 0);

    argc = 0;
    argv[argc++] = mprAsprintf(gp, -1, "%s/runProgram%s", mprGetAppDir(gp), BLD_EXE);
    argv[argc++] = "0";
    argv[argc++] = "a";
    argv[argc++] = "b";
    argv[argc++] = "c";
    argv[argc++] = NULL;

    i = 0;
    env[i++] = "CMD_ENV=xyz";
    env[i++] = NULL;

    mprSetCmdCallback(cmd, withDataCallback, gp);
    
    rc = mprStartCmd(cmd, argc, argv, env, MPR_CMD_IN | MPR_CMD_OUT | MPR_CMD_ERR);
    assert(rc == 0);
    if (rc != 0) {
        mprFree(cmd);
        return;
    }

    /*
     *  Write data to the child's stdin. We write so little data, this can't block.
     */
    fd = mprGetCmdFd(cmd, MPR_CMD_STDIN);
    assert(fd > 0);

    for (i = 0; i < 10; i++) {
        mprSprintf(line, sizeof(line), "line %d\n", i);
        len = strlen(line);
        rc = write(fd, line, len);
        assert(rc == len);
    }
    mprCloseCmdFd(cmd, MPR_CMD_STDIN);
    assert(mprWaitForCmd(cmd, MPR_TEST_SLEEP) == 0);

    /*
     *  Now analyse returned data
     */
    buf = (MprBuf*) gp->data;
    assert(mprGetBufLength(buf) > 0);
    if (mprGetBufLength(buf) > 0) {
        data = mprGetBufStart(buf);
        // print("GOT %s", data);
        s = mprStrTok(data, "\n\r", &tok);
        assert(s != 0);
        assert(match(s, "a b c") == 0);

        s = mprStrTok(0, "\n\r", &tok);
        assert(s != 0);
        assert(match(s, "CMD_ENV=xyz") == 0);

        for (i = 0; i < 10; i++) { 
            mprSprintf(line, sizeof(line), "line %d", i);
            s = mprStrTok(0, "\n\r", &tok);
            assert(s != 0);
            assert(match(s, line) == 0);
        }
        s = mprStrTok(0, "\n\r", &tok);
        assert(match(s, "END") == 0);
    }

    rc = mprGetCmdExitStatus(cmd, &status);
    assert(rc == 0);
    assert(status == 0);
    mprFree(cmd);
}


static void testExitCode(MprTestGroup *gp)
{
    MprCmd  *cmd;
    char    *result, command[MPR_MAX_FNAME];
    int     i, status;

    cmd = mprCreateCmd(gp);
    assert(cmd != 0);

    for (i = 0; i < 1; i++) {
        mprSprintf(command, sizeof(command), "%s/runProgram%s %d", mprGetAppDir(gp), BLD_EXE, i);
        status = mprRunCmd(cmd, command, &result, NULL, MPR_CMD_IN);
        assert(result != NULL);
        assert(status == i);
        if (status != i) {
            mprLog(gp, 0, "Status %d, result %s", status, result);
        }
    }
    mprFree(cmd);
}


/*
 *  Test invoking a command with no stdout/stderr capture.
 */
static void testNoCapture(MprTestGroup *gp)
{
    MprCmd      *cmd;
    char        command[80];
    int         rc, status;

    cmd = mprCreateCmd(gp);
    assert(cmd != 0);

    mprSprintf(command, sizeof(command), "%s/runProgram%s 99", mprGetAppDir(gp), BLD_EXE);
    status = mprRunCmd(cmd, command, NULL, NULL, MPR_CMD_IN);
    assert(status == 99);

    rc = mprGetCmdExitStatus(cmd, &status);
    assert(rc == 0);
    assert(status == 99);
    mprFree(cmd);
}



MprTestDef testCmd = {
    "cmd", 0, 0, 0,
    {
        MPR_TEST(0, testCreateCmd),
        MPR_TEST(0, testRunCmd),
        MPR_TEST(0, testExitCode),
        MPR_TEST(0, testWithData),
        MPR_TEST(0, testNoCapture),
        MPR_TEST(0, 0),
    },
};

#endif /* BLD_FEATURE_CMD */

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
