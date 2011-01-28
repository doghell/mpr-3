/* 
 *  mprCmd.c - Run external commands
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

#if BLD_FEATURE_CMD
/******************************* Forward Declarations *************************/

static void closeFiles(MprCmd *cmd);
static int  cmdDestructor(MprCmd *cmd);
static int  makeChannel(MprCmd *cmd, int index);
static void resetCmd(MprCmd *cmd);
static int sanitizeArgs(MprCmd *cmd, int argc, char **argv, char **env);
static int  startProcess(MprCmd *cmd);
static int stdoutCallback(MprCmd *cmd, int mask);
static int stderrCallback(MprCmd *cmd, int mask);

#if BLD_UNIX_LIKE
static char **fixenv(MprCmd *cmd);
#endif
#if VXWORKS
typedef int (*MprCmdTaskFn)(int argc, char **argv, char **envp);
static void cmdTaskEntry(char *program, MprCmdTaskFn entry, int cmdArg);
#endif

/************************************* Code ***********************************/

MprCmdService *mprCreateCmdService(Mpr *mpr)
{
    MprCmdService   *cs;

    cs = (MprCmdService*) mprAllocObjWithDestructorZeroed(mpr, MprCmd, cmdDestructor);
    if (cs == 0) {
        return 0;
    }
    cs->cmds = mprCreateList(cs);
#if BLD_FEATURE_MULTITHREAD
    cs->mutex = mprCreateLock(cs);
#endif
    return cs;
}


MprCmd *mprCreateCmd(MprCtx ctx)
{
    MprCmdService   *cs;
    MprCmd          *cmd;
    MprCmdFile      *files;
    int             i;
    
    cmd = mprAllocObjWithDestructorZeroed(ctx, MprCmd, cmdDestructor);
    if (cmd == 0) {
        return 0;
    }
    cmd->completeCond = mprCreateCond(cmd);
    cmd->timeoutPeriod = MPR_TIMEOUT_CMD;
    cmd->timestamp = mprGetTime(cmd);
    cmd->forkCallback = (MprForkCallback) closeFiles;

#if VXWORKS
    cmd->startCond = semCCreate(SEM_Q_PRIORITY, SEM_EMPTY);
    cmd->exitCond = semCCreate(SEM_Q_PRIORITY, SEM_EMPTY);
#endif
    files = cmd->files;
    for (i = 0; i < MPR_CMD_MAX_PIPE; i++) {
        files[i].clientFd = -1;
        files[i].fd = -1;
    }
#if BLD_FEATURE_MULTITHREAD
    cmd->mutex = mprCreateLock(cmd);
#endif
    cs = mprGetMpr(ctx)->cmdService;
    mprLock(cs->mutex);
    mprAddItem(cs->cmds, cmd);
    mprUnlock(cs->mutex);
    return cmd;
}


#if VXWORKS
static void vxCmdDestructor(MprCmd *cmd)
{
    MprCmdFile      *files;
    int             i;

    if (cmd->startCond) {
        semDelete(cmd->startCond);
    }
    if (cmd->exitCond) {
        semDelete(cmd->exitCond);
    }
    files = cmd->files;
    for (i = 0; i < MPR_CMD_MAX_PIPE; i++) {
        if (files[i].name) {
            DEV_HDR *dev;
#if _WRS_VXWORKS_MAJOR >= 6
            cchar   *tail;
#else
            char    *tail;
#endif
            if ((dev = iosDevFind(files[i].name, &tail)) != NULL) {
                iosDevDelete(dev);
            }
        }
    }
}
#endif


static int cmdDestructor(MprCmd *cmd)
{
    MprCmdService   *cs;

    resetCmd(cmd);
#if VXWORKS
    vxCmdDestructor(cmd);
#endif
    cs = mprGetMpr(cmd)->cmdService;
    mprLock(cs->mutex);
    mprRemoveItem(cs->cmds, cmd);
    mprUnlock(cs->mutex);
    return 0;
}


static void resetCmd(MprCmd *cmd)
{
    MprCmdFile      *files;
    int             i;

    files = cmd->files;
    for (i = 0; i < MPR_CMD_MAX_PIPE; i++) {
        if (cmd->handlers[i]) {
            mprFree(cmd->handlers[i]);
            cmd->handlers[i] = 0;
        }
        if (files[i].clientFd >= 0) {
            close(files[i].clientFd);
            files[i].clientFd = -1;
        }
        if (files[i].fd >= 0) {
            close(files[i].fd);
            files[i].fd = -1;
        }
    }
    cmd->eofCount = 0;
    cmd->status = -1;
    mprResetCond(cmd->completeCond);

    if (cmd->pid && !(cmd->flags & MPR_CMD_DETACH)) {
        mprStopCmd(cmd);
        mprReapCmd(cmd, 0);
        mprSignalCond(cmd->completeCond);
    }
}


void mprDisconnectCmd(MprCmd *cmd)
{
    MprCmdFile      *files;
    int             i;

    files = cmd->files;

    lock(cmd);
    for (i = 0; i < MPR_CMD_MAX_PIPE; i++) {
        if (cmd->handlers[i]) {
            mprDisconnectWaitHandler(cmd->handlers[i]);
        }
    }
    unlock(cmd);
}


/*
 *  Close a command channel. Must be able to be called redundantly.
 */
void mprCloseCmdFd(MprCmd *cmd, int channel)
{
    mprAssert(0 <= channel && channel <= MPR_CMD_MAX_PIPE);

    /*
     *  Disconnect but don't free. This prevents some races with callbacks.
     */
    if (cmd->handlers[channel]) {
        mprDisconnectWaitHandler(cmd->handlers[channel]);
    }

    if (cmd->files[channel].fd != -1) {
        close(cmd->files[channel].fd);
        cmd->files[channel].fd = -1;
#if BLD_WIN_LIKE
        cmd->files[channel].handle = 0;
#endif
        if (channel != MPR_CMD_STDIN) {
            if (++cmd->eofCount >= cmd->requiredEof) {
                mprReapCmd(cmd, MPR_TIMEOUT_STOP_TASK);
                mprSignalCond(cmd->completeCond);
            }
        }
    }
}


int mprIsCmdComplete(MprCmd *cmd)
{
    return cmd->eofCount >= cmd->requiredEof;
}


/*
 *  Default callback routine for the mprRunCmd routines. Uses may supply their own callback instead of this routine. 
 *  The callback is run whenever there is I/O to read/write to the CGI gateway.
 */
static int cmdCallback(MprCmd *cmd, int channel, void *data)
{
    MprBuf      *buf;
    int         len, space;

    /*
     *  Note: stdin, stdout and stderr are named from the client's perspective
     */
    buf = 0;
    switch (channel) {
    case MPR_CMD_STDIN:
        return 0;
    case MPR_CMD_STDOUT:
        buf = cmd->stdoutBuf;
        break;
    case MPR_CMD_STDERR:
        buf = cmd->stderrBuf;
        break;
    }

    /*
     *  Read and aggregate the result into a single string
     */
    space = mprGetBufSpace(buf);
    if (space < (MPR_BUFSIZE / 4)) {
        if (mprGrowBuf(buf, MPR_BUFSIZE) < 0) {
            mprCloseCmdFd(cmd, channel);
            return 0;
        }
        space = mprGetBufSpace(buf);
    }

    len = mprReadCmdPipe(cmd, channel, mprGetBufEnd(buf), space);
    if (len <= 0) {
        if (len == 0 || (len < 0 && !(errno == EAGAIN || EWOULDBLOCK))) {
            if (channel == MPR_CMD_STDOUT && cmd->flags & MPR_CMD_ERR) {
                /*
                 *  Now that stdout is complete, enable stderr to receive an EOF or any error output.
                 *  This is serialized to eliminate both stdin and stdout events on different threads at the same time.
                 *  Do before closing as the stderr event may come on another thread and we want to ensure avoid locking.
                 */
                mprCloseCmdFd(cmd, channel);
            } else {
                mprCloseCmdFd(cmd, channel);
            }
            return 0;
        }
    } else {
        mprAdjustBufEnd(buf, len);
    }
    return 0;
}


/*
 *  Run a simple blocking command. See arg usage below in mprRunCmdV.
 */
int mprRunCmd(MprCmd *cmd, cchar *command, char **out, char **err, int flags)
{
    char    **argv;
    int     argc;

    if (mprMakeArgv(cmd, NULL, command, &argc, &argv) < 0 || argv == 0) {
        return 0;
    }
    return mprRunCmdV(cmd, argc, argv, out, err, flags);
}


/*
 *  This routine runs a command and waits for its completion. Stdoutput and Stderr are returned in *out and *err 
 *  respectively. The command returns the exit status of the command.
 *  Valid flags are:
 *      MPR_CMD_NEW_SESSION     Create a new session on Unix
 *      MPR_CMD_SHOW            Show the commands window on Windows
 *      MPR_CMD_IN              Connect to stdin
 */
int mprRunCmdV(MprCmd *cmd, int argc, char **argv, char **out, char **err, int flags)
{
    int     rc, status;

    if (err) {
        *err = 0;
        flags |= MPR_CMD_ERR;
    } else {
        flags &= ~MPR_CMD_ERR;
    }
    if (out) {
        *out = 0;
        flags |= MPR_CMD_OUT;
    } else {
        flags &= ~MPR_CMD_OUT;
    }

    if (flags & MPR_CMD_OUT) {
        mprFree(cmd->stdoutBuf);
        cmd->stdoutBuf = mprCreateBuf(cmd, MPR_BUFSIZE, -1);
    }
    if (flags & MPR_CMD_ERR) {
        mprFree(cmd->stderrBuf);
        cmd->stderrBuf = mprCreateBuf(cmd, MPR_BUFSIZE, -1);
    }
    mprSetCmdCallback(cmd, cmdCallback, NULL);
    lock(cmd);

    rc = mprStartCmd(cmd, argc, argv, NULL, flags);

    /*
     *  Close the pipe connected to the client's stdin
     */
    if (cmd->files[MPR_CMD_STDIN].fd >= 0) {
        mprCloseCmdFd(cmd, MPR_CMD_STDIN);
    }
    if (rc < 0) {
        if (err) {
            if (rc == MPR_ERR_CANT_ACCESS) {
                *err = mprAsprintf(cmd, -1, "Can't access command %s", cmd->program);
            } else if (MPR_ERR_CANT_OPEN) {
                *err = mprAsprintf(cmd, -1, "Can't open standard I/O for command %s", cmd->program);
            } else if (rc == MPR_ERR_CANT_CREATE) {
                *err = mprAsprintf(cmd, -1, "Can't create process for %s", cmd->program);
            }
        }
        unlock(cmd);
        return rc;
    }
    if (cmd->flags & MPR_CMD_DETACH) {
        unlock(cmd);
        return 0;
    }
    unlock(cmd);
    if (mprWaitForCmd(cmd, -1) < 0) {
        return MPR_ERR_NOT_READY;
    }
    lock(cmd);

    if (mprGetCmdExitStatus(cmd, &status) < 0) {
        unlock(cmd);
        return MPR_ERR;
    }
    if (err && flags & MPR_CMD_ERR) {
        mprAddNullToBuf(cmd->stderrBuf);
        *err = mprGetBufStart(cmd->stderrBuf);
    }
    if (out && flags & MPR_CMD_OUT) {
        mprAddNullToBuf(cmd->stdoutBuf);
        *out = mprGetBufStart(cmd->stdoutBuf);
    }
    unlock(cmd);
    return status;
}


/*
 *  Start the command to run (stdIn and stdOut are named from the client's perspective). This is the lower-level way to 
 *  run a command. The caller needs to do code like mprRunCmd() themselves to wait for completion and to send/receive data.
 *  The routine does not wait. Callers must call mprWaitForCmd to wait for the command to complete.
 */
int mprStartCmd(MprCmd *cmd, int argc, char **argv, char **envp, int flags)
{
    MprPath     info;
    char        *program;
    int         rc;

    mprAssert(argv);
    mprAssert(argc > 0);

    if (argc <= 0 || argv == NULL || argv[0] == NULL) {
        return MPR_ERR_BAD_STATE;
    }
    resetCmd(cmd);
    program = argv[0];
    cmd->program = program;
    cmd->flags = flags;

    if (sanitizeArgs(cmd, argc, argv, envp) < 0) {
        return MPR_ERR_NO_MEMORY;
    }
    if (access(program, X_OK) < 0) {
        program = mprJoinPathExt(cmd, program, BLD_EXE);
        if (access(program, X_OK) < 0) {
            mprLog(cmd, 1, "cmd: can't access %s, errno %d", program, mprGetOsError());
            return MPR_ERR_CANT_ACCESS;
        }
    }
    if (mprGetPathInfo(cmd, program, &info) == 0 && info.isDir) {
        mprLog(cmd, 1, "cmd: program \"%s\", is a directory", program);
        return MPR_ERR_CANT_ACCESS;
    }

#if CYGWIN
    mprGlobalLock(cmd);
#endif
    if (mprMakeCmdIO(cmd) < 0) {
#if CYGWIN
        mprGlobalUnlock(cmd);
#endif
        return MPR_ERR_CANT_OPEN;
    }

    /*
     *  Determine how many end-of-files will be seen when the child dies
     */
    cmd->requiredEof = 0;
    if (cmd->flags & MPR_CMD_OUT) {
        cmd->requiredEof++;
    }
    if (cmd->flags & MPR_CMD_ERR) {
        cmd->requiredEof++;
    }

#if UNUSED && KEEP && NON_BLOCKING
#if BLD_UNIX_LIKE || VXWORKS
    {
        int     stdinFd, stdoutFd, stderrFd, nonBlock;
      
        stdinFd = cmd->files[MPR_CMD_STDIN].fd; 
        stdoutFd = cmd->files[MPR_CMD_STDOUT].fd; 
        stderrFd = cmd->files[MPR_CMD_STDERR].fd; 
        nonBlock = 1;

        /*
         *  Put the stdout and stderr into non-blocking mode. Windows can't do this because both ends of the pipe
         *  share the same blocking mode (Ugh!).
         */
#if VXWORKS
        if (stdoutFd >= 0) {
            ioctl(stdoutFd, FIONBIO, (int) &nonBlock);
        }
        if (stderrFd >= 0) {
            ioctl(stderrFd, FIONBIO, (int) &nonBlock);
        }
#else
        if (stdoutFd >= 0) {
            fcntl(stdoutFd, F_SETFL, fcntl(stdoutFd, F_GETFL) | O_NONBLOCK);
        }
        if (stderrFd >= 0) {
            fcntl(stderrFd, F_SETFL, fcntl(stderrFd, F_GETFL) | O_NONBLOCK);
        }
#endif
        if (stdoutFd >= 0) {
            cmd->handlers[MPR_CMD_STDOUT] = mprCreateWaitHandler(cmd, stdoutFd, MPR_READABLE,
                (MprWaitProc) stdoutCallback, cmd, MPR_NORMAL_PRIORITY, MPR_WAIT_THREAD);
        }
        if (stderrFd >= 0) {
            cmd->handlers[MPR_CMD_STDERR] = mprCreateWaitHandler(cmd, stderrFd, MPR_READABLE,
                (MprWaitProc) stderrCallback, cmd, MPR_NORMAL_PRIORITY, MPR_WAIT_THREAD);
            if (stdoutFd >= 0) {
                /*
                 *  Delay enabling stderr events until stdout is complete
                 */
                mprDisableWaitEvents(cmd->handlers[MPR_CMD_STDERR]);
            }
        }
    }
#endif
#endif
    rc = startProcess(cmd);
#if CYGWIN
    mprGlobalUnlock(cmd);
#endif
    return rc;
}


int mprMakeCmdIO(MprCmd *cmd)
{
    MprCmdFile  *files;
    int         rc;

    files = cmd->files;

    rc = 0;
    if (cmd->flags & MPR_CMD_IN) {
        rc += makeChannel(cmd, MPR_CMD_STDIN);
    }
    if (cmd->flags & MPR_CMD_OUT) {
        rc += makeChannel(cmd, MPR_CMD_STDOUT);
    }
    if (cmd->flags & MPR_CMD_ERR) {
        rc += makeChannel(cmd, MPR_CMD_STDERR);
    }
    return rc;
}


/*
 *  Stop the command
 */
void mprStopCmd(MprCmd *cmd)
{
    mprLog(cmd, 7, "cmd: stop");

    if (cmd->pid) {
#if BLD_WIN_LIKE
        TerminateProcess(cmd->process, 2);
#elif VXWORKS
        taskDelete(cmd->pid);
#else
        kill(cmd->pid, SIGTERM);
#endif
    }
}


int mprReadCmdPipe(MprCmd *cmd, int channel, char *buf, int bufsize)
{
#if BLD_WIN_LIKE && !WINCE
    int     count, rc;

    rc = PeekNamedPipe(cmd->files[channel].handle, NULL, 0, NULL, &count, NULL);
    if (rc && count > 0) {
        return read(cmd->files[channel].fd, buf, bufsize);
    }
    if (cmd->process == 0) {
        return 0;
    }
    /*
     *  No waiting. Use this just to check if the process has exited and thus EOF on the pipe.
     */
    if (WaitForSingleObject(cmd->process, 0) == WAIT_OBJECT_0) {
        return 0;
    }
    errno = EAGAIN;
    return -1;

#elif VXWORKS && UNUSED && KEEP
    /*
        Only needed when using non-blocking I/O
     */
    int     rc;
    rc = read(cmd->files[channel].fd, buf, bufsize);

    /*
     *  VxWorks can't signal EOF on non-blocking pipes. Need a pattern indicator.
     */
    if (rc == MPR_CMD_VXWORKS_EOF_LEN && strncmp(buf, MPR_CMD_VXWORKS_EOF, MPR_CMD_VXWORKS_EOF_LEN) == 0) {
        /* EOF */
        return 0;

    } else if (rc == 0) {
        rc = -1;
        errno = EAGAIN;
    }
    return rc;

#else
    return read(cmd->files[channel].fd, buf, bufsize);
#endif
}


int mprWriteCmdPipe(MprCmd *cmd, int channel, char *buf, int bufsize)
{
#if BLD_WIN_LIKE
    /*
     *  No waiting. Use this just to check if the process has exited and thus EOF on the pipe.
     */
    if (WaitForSingleObject(cmd->process, 0) == WAIT_OBJECT_0) {
        return -1;
    }
#endif
    return write(cmd->files[channel].fd, buf, bufsize);
}


#if UNUSED && KEEP
void mprEnableCmdEvents(MprCmd *cmd, int channel)
{
#if BLD_UNIX_LIKE || VXWORKS
    if (cmd->handlers[channel]) {
        mprEnableWaitEvents(cmd->handlers[channel]);
    }
#endif
}


void mprDisableCmdEvents(MprCmd *cmd, int channel)
{
#if BLD_UNIX_LIKE || VXWORKS
    if (cmd->handlers[channel]) {
        mprDisableWaitEvents(cmd->handlers[channel]);
    }
#endif
}
#endif


#if BLD_WIN_LIKE && !WINCE
/*
 *  Service I/O and return a count of characters that can be read without blocking. If the proces has completed,
 *  then return 1 to indicate that EOF can be read.
 */
static int serviceCmdEvents(MprCmd *cmd, int channel, int timeout)
{
    int     rc, count, status;

    if (mprGetDebugMode(cmd)) {
        timeout = MAXINT;
    }
    if (cmd->files[channel].handle) {
        rc = PeekNamedPipe(cmd->files[channel].handle, NULL, 0, NULL, &count, NULL);
        if (rc && count > 0) {
            return count;
        }
    }
    if (cmd->process == 0) {
        return 1;
    }
    if ((status = WaitForSingleObject(cmd->process, timeout)) == WAIT_OBJECT_0) {
        if (cmd->requiredEof == 0) {
            mprReapCmd(cmd, MPR_TIMEOUT_STOP_TASK);
            mprSignalCond(cmd->completeCond);
            return 0;
        }
        return 1;
    }
}
#endif /* BLD_WIN_LIKE && !WINCE */


/*
 *  Poll for I/O events on the CGI pipes
 */
void mprPollCmdPipes(MprCmd *cmd, int timeout)
{
#if BLD_WIN_LIKE && !WINCE
    if (cmd->files[MPR_CMD_STDOUT].handle) {
        if (serviceCmdEvents(cmd, MPR_CMD_STDOUT, timeout) > 0 && (cmd->flags & MPR_CMD_OUT)) {
            stdoutCallback(cmd, MPR_READABLE);
        }
    } else if (cmd->files[MPR_CMD_STDERR].handle) {
        if (serviceCmdEvents(cmd, MPR_CMD_STDERR, timeout) > 0 && (cmd->flags & MPR_CMD_ERR)) {
            stderrCallback(cmd, MPR_READABLE);
        }
    }
#else
    if (cmd->files[MPR_CMD_STDOUT].fd >= 0) {
        if (mprWaitForSingleIO(cmd, cmd->files[MPR_CMD_STDOUT].fd, MPR_READABLE, timeout)) {
            stdoutCallback(cmd, MPR_READABLE);
        }
    } else if (cmd->files[MPR_CMD_STDERR].fd >= 0) {
        if (mprWaitForSingleIO(cmd, cmd->files[MPR_CMD_STDERR].fd, MPR_READABLE, timeout)) {
            stderrCallback(cmd, MPR_READABLE);
        }
    }
#endif
}

/*
 *  Wait for a command to complete. Return 0 if the command completed, otherwise return MPR_ERR_TIMEOUT. 
 *  This will call mprReapCmd if requried.
 */
int mprWaitForCmd(MprCmd *cmd, int timeout)
{
    MprTime     mark;
    int         complete, rc;

    if (timeout < 0) timeout = MAXINT;
    if (mprGetDebugMode(cmd)) timeout = MAXINT;

    mark = mprGetTime(cmd);
    complete = 0;

    do {
        if (cmd->requiredEof == 0) {
            if (mprReapCmd(cmd, 10) == 0) {
                mprSignalCond(cmd->completeCond);
                return 0;
            }
        }
        mprPollCmdPipes(cmd, timeout);
        rc = mprWaitForCondWithService(cmd->completeCond, 10);
        if (rc == 0) {
            complete++;
            break;
        } else if (rc != MPR_ERR_TIMEOUT) {
            mprAssert(0);
        }
    } while (mprGetElapsedTime(cmd, mark) <= timeout);

    if (!complete) {
        mprLog(cmd, 7, "cmd: mprWaitForCmd: timeout waiting for command to complete");
        return MPR_ERR_TIMEOUT;
    }
    if (cmd->pid) {
        mprReapCmd(cmd, MPR_TIMEOUT_STOP_TASK);
    }
    mprLog(cmd, 7, "cmd: waitForChild: status %d", cmd->status);
    return 0;
}


/*
 *  Collect the child's exit status. The initiating thread must do this on uClibc. 
 *  Return zero if the exit status is successfully reaped. Return -1 if an error 
 *  and return > 0 if process still running.
 */
int mprReapCmd(MprCmd *cmd, int timeout)
{
    MprTime     mark;

    mprAssert(cmd->pid);

    if (timeout < 0) {
        timeout = MAXINT;
    }
    mark = mprGetTime(cmd);

    while (cmd->pid) {
#if BLD_UNIX_LIKE
        int     status, waitrc;
        status = 0;
        if ((waitrc = waitpid(cmd->pid, &status, WNOHANG | __WALL)) < 0) {
            mprAssert(0);
            mprLog(cmd, 0, "waitpid failed for pid %d, errno %d", cmd->pid, errno);
            return MPR_ERR_CANT_READ;

        } else if (waitrc == cmd->pid) {
            if (!WIFSTOPPED(status)) {
                if (WIFEXITED(status)) {
                    cmd->status = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    cmd->status = WTERMSIG(status);
                }
                cmd->pid = 0;
            }
            break;
            
        } else {
            mprAssert(waitrc == 0);
        }
#endif
#if VXWORKS
        /*
         *  The command exit status (cmd->status) is set in cmdTaskEntry
         */
        if (semTake(cmd->exitCond, MPR_TIMEOUT_STOP_TASK) != OK) {
            mprError(cmd, "cmd: child %s did not exit, errno %d", cmd->program);
            return MPR_ERR_CANT_CREATE;
        }
        semDelete(cmd->exitCond);
        cmd->exitCond = 0;
        cmd->pid = 0;
#endif
#if BLD_WIN_LIKE
        int     status, rc;
        if ((rc = WaitForSingleObject(cmd->process, 10)) != WAIT_OBJECT_0) {
            if (rc == WAIT_TIMEOUT) {
                return -MPR_ERR_TIMEOUT;
            }
            mprLog(cmd, 6, "cmd: WaitForSingleObject no child to reap rc %d, %d", rc, GetLastError());
            return MPR_ERR_CANT_READ;
        }
        if (GetExitCodeProcess(cmd->process, (ulong*) &status) == 0) {
            mprLog(cmd, 7, "cmd: GetExitProcess error");
            return MPR_ERR_CANT_READ;
        }
        if (status != STILL_ACTIVE) {
            cmd->status = status;
            CloseHandle(cmd->process);
            CloseHandle(cmd->thread);
            cmd->process = 0;
            cmd->pid = 0;
            break;
        }
#endif
        if (mprGetElapsedTime(cmd, mark) > timeout) {
            break;
        }
        /* Prevent busy waiting */
        mprSleep(cmd, 10);
    }
    return (cmd->pid == 0) ? 0 : 1;
}


static int stdoutCallback(MprCmd *cmd, int mask)
{
    return (cmd->callback)(cmd, MPR_CMD_STDOUT, cmd->callbackData);
}


static int stderrCallback(MprCmd *cmd, int mask)
{
    return (cmd->callback)(cmd, MPR_CMD_STDERR, cmd->callbackData);
}


void mprSetCmdCallback(MprCmd *cmd, MprCmdProc proc, void *data)
{
    cmd->callback = proc;
    cmd->callbackData = data;
}


int mprGetCmdExitStatus(MprCmd *cmd, int *statusp)
{
    mprAssert(statusp);

    if (cmd->pid) {
        mprReapCmd(cmd, MPR_TIMEOUT_STOP_TASK);
        if (cmd->pid) {
            return MPR_ERR_NOT_READY;
        }
    }
    *statusp = cmd->status;
    return 0;
}


bool mprIsCmdRunning(MprCmd *cmd)
{
    return cmd->pid > 0;
}


void mprSetCmdTimeout(MprCmd *cmd, int timeout)
{
    cmd->timeoutPeriod = timeout;
}


int mprGetCmdFd(MprCmd *cmd, int channel) 
{ 
    return cmd->files[channel].fd; 
}


MprBuf *mprGetCmdBuf(MprCmd *cmd, int channel)
{
    return (channel == MPR_CMD_STDOUT) ? cmd->stdoutBuf : cmd->stderrBuf;
}


void mprSetCmdDir(MprCmd *cmd, cchar *dir)
{
    mprAssert(dir && *dir);

    mprFree(cmd->dir);
    cmd->dir = mprStrdup(cmd, dir);
}


/*
 *  Sanitize args. Convert "/" to "\" and converting '\r' and '\n' to spaces, quote all args and put the program as argv[0].
 */
static int sanitizeArgs(MprCmd *cmd, int argc, char **argv, char **env)
{
#if VXWORKS
    cmd->argv = argv;
    cmd->argc = argc;
    cmd->env = 0;
#endif
#if BLD_UNIX_LIKE
    char    *cp;
    int     index, i, hasPath, hasLibPath;

    cmd->argv = argv;
    cmd->argc = argc;
    cmd->env = 0;

    if (env) {
        for (i = 0; env && env[i]; i++) {
            mprLog(cmd, 6, "cmd: env[%d]: %s", i, env[i]);
        }
        if ((cmd->env = mprAlloc(cmd, (i + 3) * sizeof(char*))) == NULL) {
            return MPR_ERR_NO_MEMORY;
        }
        hasPath = hasLibPath = 0;
        for (index = i = 0; env && env[i]; i++) {
            mprLog(cmd, 6, "cmd: env[%d]: %s", i, env[i]);
            if (strncmp(env[i], "PATH=", 5) == 0) {
                hasPath++;
            } else if  (strncmp(env[i], LD_LIBRARY_PATH "=", 16) == 0) {
                hasLibPath++;
            }
            cmd->env[index++] = env[i];
        }

        /*
         *  Add PATH and LD_LIBRARY_PATH 
         */
        if (!hasPath && (cp = getenv("PATH")) != 0) {
            cmd->env[index++] = mprAsprintf(cmd, MPR_MAX_STRING, "PATH=%s", cp);
        }
        if (!hasLibPath && (cp = getenv(LD_LIBRARY_PATH)) != 0) {
            cmd->env[index++] = mprAsprintf(cmd, MPR_MAX_STRING, "%s=%s", LD_LIBRARY_PATH, cp);
        }
        cmd->env[index++] = '\0';
        for (i = 0; i < argc; i++) {
            mprLog(cmd, 4, "cmd: arg[%d]: %s", i, argv[i]);
        }
        for (i = 0; cmd->env[i]; i++) {
            mprLog(cmd, 4, "cmd: env[%d]: %s", i, cmd->env[i]);
        }
    }
#endif

#if BLD_WIN_LIKE
    char    *program, *SYSTEMROOT, **ep, **ap, *destp, *cp, *progBuf, *localArgv[2], *saveArg0, *PATH, *endp;
    int     i, len, hasPath, hasSystemRoot;

    mprAssert(argc > 0 && argv[0] != NULL);

    cmd->argv = argv;
    cmd->argc = argc;

    program = argv[0];
    progBuf = mprAlloc(cmd, (int) strlen(program) * 2 + 1);
    strcpy(progBuf, program);
    program = progBuf;

    for (cp = program; *cp; cp++) {
        if (*cp == '/') {
            *cp = '\\';
        } else if (*cp == '\r' || *cp == '\n') {
            *cp = ' ';
        }
    }
    if (*program == '\"') {
        if ((cp = strrchr(++program, '"')) != 0) {
            *cp = '\0';
        }
    }

    if (argv == 0) {
        argv = localArgv;
        argv[1] = 0;
        saveArg0 = program;
    } else {
        saveArg0 = argv[0];
    }
    /*
     *  Set argv[0] to the program name while creating the command line. Restore later
     */
    argv[0] = program;

    /*
     *  Determine the command line length and arg count
     */
    argc = 0;
    for (len = 0, ap = argv; *ap; ap++) {
        len += (int) strlen(*ap) + 1 + 2;         /* Space and possible quotes */
        argc++;
    }
    cmd->command = (char*) mprAlloc(cmd, len + 1);
    cmd->command[len] = '\0';
    
    /*
     *  Add quotes to all args that have spaces in them including "program"
     */
    destp = cmd->command;
    for (ap = &argv[0]; *ap; ) {
        cp = *ap;
        if ((strchr(cp, ' ') != 0) && cp[0] != '\"') {
            *destp++ = '\"';
            strcpy(destp, cp);
            destp += strlen(cp);
            *destp++ = '\"';
        } else {
            strcpy(destp, cp);
            destp += strlen(cp);
        }
        if (*++ap) {
            *destp++ = ' ';
        }
    }
    *destp = '\0';
    argv[0] = saveArg0;

    for (i = 0; i < argc; i++) {
        mprLog(cmd, 4, "cmd: arg[%d]: %s", i, argv[i]);
    }

    /*
     *  Now work on the environment. Windows has a block of null separated strings with a trailing null.
     */
    cmd->env = 0;
    if (env) {
        for (hasSystemRoot =  hasPath = len = 0, ep = env; ep && *ep; ep++) {
            len += (int) strlen(*ep) + 1;
            if (strncmp(*ep, "PATH=", 5) == 0) {
                hasPath++;
            } else if (strncmp(*ep, "SYSTEMROOT=", 11) == 0) {
                hasSystemRoot++;
            }
        }
        if (!hasSystemRoot && (SYSTEMROOT = getenv("SYSTEMROOT")) != 0) {
            len += 11 + (int) strlen(SYSTEMROOT) + 1;
        }
        if (!hasPath && (PATH = getenv("PATH")) != 0) {
            len += 5 + (int) strlen(PATH) + 1;
        }
        len += 2;       /* Windows requires 2 nulls for the block end */

        destp = (char*) mprAlloc(cmd, len);
        endp = &destp[len];
        cmd->env = (char**) destp;
        for (ep = env; ep && *ep; ep++) {
            mprLog(cmd, 4, "cmd: env[%d]: %s", i, *ep);
            strcpy(destp, *ep);
            mprLog(cmd, 7, "cmd: Set env variable: %s", destp);
            destp += strlen(*ep) + 1;
        }
        if (!hasSystemRoot) {
            mprSprintf(destp, endp - destp - 1, "SYSTEMROOT=%s", SYSTEMROOT);
            destp += 12 + strlen(SYSTEMROOT);
        }
        if (!hasPath) {
            mprSprintf(destp, endp - destp - 1, "PATH=%s", PATH);
            destp += 6 + strlen(PATH);
        }
        *destp++ = '\0';
        *destp++ = '\0';                        /* Windows requires two nulls */
        mprAssert(destp <= endp);
#if TEST
        for (cp = (char*) cmd->env; *cp; cp++) {
            print("ENV %s\n", cp);
            cp += strlen(cp);
        }
#endif
    }
#endif /* BLD_WIN_LIKE */
    return 0;
}


#if BLD_WIN_LIKE
static int startProcess(MprCmd *cmd)
{
    PROCESS_INFORMATION procInfo;
    STARTUPINFO         startInfo;
    int                 err;

    memset(&startInfo, 0, sizeof(startInfo));
    startInfo.cb = sizeof(startInfo);

    startInfo.dwFlags = STARTF_USESHOWWINDOW;
    if (cmd->flags & MPR_CMD_SHOW) {
        startInfo.wShowWindow = SW_SHOW;
    } else {
        startInfo.wShowWindow = SW_HIDE;
    }
    startInfo.dwFlags |= STARTF_USESTDHANDLES;

    if (cmd->flags & MPR_CMD_IN) {
        if (cmd->files[MPR_CMD_STDIN].clientFd > 0) {
            startInfo.hStdInput = (HANDLE) _get_osfhandle(cmd->files[MPR_CMD_STDIN].clientFd);
        }
    } else {
        startInfo.hStdInput = (HANDLE) _get_osfhandle((int) fileno(stdin));
    }
    if (cmd->flags & MPR_CMD_OUT) {
        if (cmd->files[MPR_CMD_STDOUT].clientFd > 0) {
            startInfo.hStdOutput = (HANDLE)_get_osfhandle(cmd->files[MPR_CMD_STDOUT].clientFd);
        }
    } else {
        startInfo.hStdOutput = (HANDLE)_get_osfhandle((int) fileno(stdout));
    }
    if (cmd->flags & MPR_CMD_ERR) {
        if (cmd->files[MPR_CMD_STDERR].clientFd > 0) {
            startInfo.hStdError = (HANDLE) _get_osfhandle(cmd->files[MPR_CMD_STDERR].clientFd);
        }
    } else {
        startInfo.hStdError = (HANDLE) _get_osfhandle((int) fileno(stderr));
    }

    if (! CreateProcess(0, cmd->command, 0, 0, 1, 0, cmd->env, cmd->dir, &startInfo, &procInfo)) {
        err = mprGetOsError();
        if (err == ERROR_DIRECTORY) {
            mprError(cmd, "Can't create process: %s, directory %s is invalid", cmd->program, cmd->dir);
        } else {
            mprError(cmd, "Can't create process: %s, %d", cmd->program, err);
        }
        return MPR_ERR_CANT_CREATE;
    }
    cmd->thread = procInfo.hThread;
    cmd->process = procInfo.hProcess;
    cmd->pid = procInfo.dwProcessId;
    return 0;
}


#if WINCE
static int makeChannel(MprCmd *cmd, int index)
{
    SECURITY_ATTRIBUTES clientAtt, serverAtt, *att;
    HANDLE              readHandle, writeHandle;
    MprCmdFile          *file;
    char                *path;
    int                 readFd, writeFd;

    memset(&clientAtt, 0, sizeof(clientAtt));
    clientAtt.nLength = sizeof(SECURITY_ATTRIBUTES);
    clientAtt.bInheritHandle = 1;

    /*
     *  Server fds are not inherited by the child
     */
    memset(&serverAtt, 0, sizeof(serverAtt));
    serverAtt.nLength = sizeof(SECURITY_ATTRIBUTES);
    serverAtt.bInheritHandle = 0;

    file = &cmd->files[index];
    path = mprGetTempPath(cmd, NULL);

    att = (index == MPR_CMD_STDIN) ? &clientAtt : &serverAtt;
    readHandle = CreateFile(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, att, OPEN_ALWAYS, 
        FILE_ATTRIBUTE_NORMAL,0);
    if (readHandle == INVALID_HANDLE_VALUE) {
        mprError(cmd, "Can't create stdio pipes %s. Err %d\n", path, mprGetOsError());
        return MPR_ERR_CANT_CREATE;
    }
    readFd = (int) (int64) _open_osfhandle((int*) readHandle, 0);

    att = (index == MPR_CMD_STDIN) ? &serverAtt: &clientAtt;
    writeHandle = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, att, OPEN_ALWAYS, 
        FILE_ATTRIBUTE_NORMAL, 0);
    writeFd = (int) _open_osfhandle((int*) writeHandle, 0);

    if (readFd < 0 || writeFd < 0) {
        mprError(cmd, "Can't create stdio pipes %s. Err %d\n", path, mprGetOsError());
        return MPR_ERR_CANT_CREATE;
    }
    if (index == MPR_CMD_STDIN) {
        file->clientFd = readFd;
        file->fd = writeFd;
        file->handle = writeHandle;
    } else {
        file->clientFd = writeFd;
        file->fd = readFd;
        file->handle = readHandle;
    }
    mprFree(path);
    return 0;
}
#else /* !WINCE */

static int makeChannel(MprCmd *cmd, int index)
{
    SECURITY_ATTRIBUTES clientAtt, serverAtt, *att;
    HANDLE              readHandle, writeHandle;
    MprCmdFile          *file;
    MprTime             now;
    char                *pipeBuf;
    int                 openMode, pipeMode, readFd, writeFd;
    static int          tempSeed = 0;

    memset(&clientAtt, 0, sizeof(clientAtt));
    clientAtt.nLength = sizeof(SECURITY_ATTRIBUTES);
    clientAtt.bInheritHandle = 1;

    /*
     *  Server fds are not inherited by the child
     */
    memset(&serverAtt, 0, sizeof(serverAtt));
    serverAtt.nLength = sizeof(SECURITY_ATTRIBUTES);
    serverAtt.bInheritHandle = 0;

    file = &cmd->files[index];
    now = ((int) mprGetTime(cmd) & 0xFFFF) % 64000;

    pipeBuf = mprAsprintf(cmd, -1, "\\\\.\\pipe\\MPR_%d_%d_%d.tmp", getpid(), (int) now, ++tempSeed);

    /*
     *  Pipes are always inbound. The file below is outbound. we swap whether the client or server
     *  inherits the pipe or file. MPR_CMD_STDIN is the clients input pipe.
     *  Pipes are blocking since both ends share the same blocking mode. Client must be blocking.
     */
    openMode = PIPE_ACCESS_INBOUND;
    pipeMode = 0;

    att = (index == MPR_CMD_STDIN) ? &clientAtt : &serverAtt;
    readHandle = CreateNamedPipe(pipeBuf, openMode, pipeMode, 1, 0, 256 * 1024, 1, att);
    if (readHandle == INVALID_HANDLE_VALUE) {
        mprError(cmd, "Can't create stdio pipes %s. Err %d\n", pipeBuf, mprGetOsError());
        return MPR_ERR_CANT_CREATE;
    }
    readFd = (int) (int64) _open_osfhandle((long) readHandle, 0);

    att = (index == MPR_CMD_STDIN) ? &serverAtt: &clientAtt;
    writeHandle = CreateFile(pipeBuf, GENERIC_WRITE, 0, att, OPEN_EXISTING, openMode, 0);
    writeFd = (int) _open_osfhandle((long) writeHandle, 0);

    if (readFd < 0 || writeFd < 0) {
        mprError(cmd, "Can't create stdio pipes %s. Err %d\n", pipeBuf, mprGetOsError());
        return MPR_ERR_CANT_CREATE;
    }
    if (index == MPR_CMD_STDIN) {
        file->clientFd = readFd;
        file->fd = writeFd;
        file->handle = writeHandle;
    } else {
        file->clientFd = writeFd;
        file->fd = readFd;
        file->handle = readHandle;
    }
    mprFree(pipeBuf);
    return 0;
}
#endif /* WINCE */


#elif BLD_UNIX_LIKE
static int startProcess(MprCmd *cmd)
{
    MprCmdFile      *files;
    int             rc, i, err;

    files = cmd->files;

    /*
     *  Create the child
     */
    cmd->pid = vfork();

    if (cmd->pid < 0) {
        mprError(cmd, "start: can't fork a new process to run %s, errno %d", cmd->program, mprGetOsError());
        return MPR_ERR_CANT_INITIALIZE;

    } else if (cmd->pid == 0) {
        /*
         *  Child
         */
        umask(022);
        if (cmd->flags & MPR_CMD_NEW_SESSION) {
            setsid();
        }
        if (cmd->dir) {
            if (chdir(cmd->dir) < 0) {
                mprLog(cmd, 0, "cmd: Can't change directory to %s", cmd->dir);
                return MPR_ERR_CANT_INITIALIZE;
            }
        }
        if (cmd->flags & MPR_CMD_IN) {
            if (files[MPR_CMD_STDIN].clientFd >= 0) {
                rc = dup2(files[MPR_CMD_STDIN].clientFd, 0);
                close(files[MPR_CMD_STDIN].fd);
            } else {
                close(0);
            }
        }
        if (cmd->flags & MPR_CMD_OUT) {
            if (files[MPR_CMD_STDOUT].clientFd >= 0) {
                rc = dup2(files[MPR_CMD_STDOUT].clientFd, 1);
                close(files[MPR_CMD_STDOUT].fd);
            } else {
                close(1);
            }
        }
        if (cmd->flags & MPR_CMD_ERR) {
            if (files[MPR_CMD_STDERR].clientFd >= 0) {
                rc = dup2(files[MPR_CMD_STDERR].clientFd, 2);
                close(files[MPR_CMD_STDERR].fd);
            } else {
                close(2);
            }
        }
        cmd->forkCallback(cmd->forkData);
        if (cmd->env) {
            rc = execve(cmd->program, cmd->argv, fixenv(cmd));
        } else {
            rc = execv(cmd->program, cmd->argv);
        }
        err = errno;
        mprPrintfError(cmd, "Can't exec %s, err %d, cwd %s\n", cmd->program, err, mprGetCurrentPath(cmd));

        /*
         *  Use _exit to avoid flushing I/O any other I/O.
         */
        _exit(-(MPR_ERR_CANT_INITIALIZE));

    } else {
        /*
         *  Close the client handles
         */
        for (i = 0; i < MPR_CMD_MAX_PIPE; i++) {
            if (files[i].clientFd >= 0) {
                close(files[i].clientFd);
                files[i].clientFd = -1;
            }
        }
    }
    return 0;
}


static int makeChannel(MprCmd *cmd, int index)
{
    MprCmdFile      *file;
    int             fds[2];

    file = &cmd->files[index];

    if (pipe(fds) < 0) {
        mprError(cmd, "Can't create stdio pipes. Err %d", mprGetOsError());
        return MPR_ERR_CANT_CREATE;
    }
    if (index == MPR_CMD_STDIN) {
        file->clientFd = fds[0];        /* read fd */
        file->fd = fds[1];              /* write fd */
    } else {
        file->clientFd = fds[1];        /* write fd */
        file->fd = fds[0];              /* read fd */
    }
    mprLog(cmd, 7, "mprMakeCmdIO: pipe handles[%d] read %d, write %d", index, fds[0], fds[1]);
    return 0;
}
#endif /* BLD_UNIX_LIKE */


#if VXWORKS
/*
    Start the command to run (stdIn and stdOut are named from the client's perspective)
 */
int startProcess(MprCmd *cmd)
{
    MprCmdTaskFn    entryFn;
    SYM_TYPE        symType;
    char            *entryPoint, *program;
    int             i, pri;

    mprLog(cmd, 4, "cmd: start %s", cmd->program);

    entryPoint = 0;
    if (cmd->env) {
        for (i = 0; cmd->env[i]; i++) {
            if (strncmp(cmd->env[i], "entryPoint=", 11) == 0) {
                entryPoint = mprStrdup(cmd, cmd->env[i]);
            }
        }
    }
    program = mprGetPathBase(cmd, cmd->program);
    if (entryPoint == 0) {
        program = mprTrimPathExtension(cmd, program);
#if BLD_HOST_CPU_ARCH == MPR_CPU_IX86 || BLD_HOST_CPU_ARCH == MPR_CPU_IX64
        entryPoint = mprStrcat(cmd, -1, "_", program, "Main", NULL);
#else
        entryPoint = mprStrcat(cmd, -1, program, "Main", NULL);
#endif
    }

    if (symFindByName(sysSymTbl, entryPoint, (char**) &entryFn, &symType) < 0) {
        if (mprLoadModule(cmd, cmd->program, NULL) == 0) {
            mprError(cmd, "start: can't load DLL %s, errno %d", program, mprGetOsError());
            return MPR_ERR_CANT_READ;
        }
        if (symFindByName(sysSymTbl, entryPoint, (char**) &entryFn, &symType) < 0) {
            mprError(cmd, "start: can't find symbol %s, errno %d", entryPoint, mprGetOsError());
            return MPR_ERR_CANT_ACCESS;
        }
    }
    taskPriorityGet(taskIdSelf(), &pri);

    /*
     *  Pass the server output file to become the client stdin.
     */
    cmd->pid = taskSpawn(entryPoint, pri, 0, MPR_DEFAULT_STACK, (FUNCPTR) cmdTaskEntry, 
        (int) cmd->program, (int) entryFn, (int) cmd, 0, 0, 0, 0, 0, 0, 0);

    if (cmd->pid < 0) {
        mprError(cmd, "start: can't create task %s, errno %d", entryPoint, mprGetOsError());
        mprFree(entryPoint);
        return MPR_ERR_CANT_CREATE;
    }

    mprLog(cmd, 7, "cmd, child taskId %d", cmd->pid);
    mprFree(entryPoint);

    if (semTake(cmd->startCond, MPR_TIMEOUT_START_TASK) != OK) {
        mprError(cmd, "start: child %s did not initialize, errno %d", cmd->program, mprGetOsError());
        return MPR_ERR_CANT_CREATE;
    }
    semDelete(cmd->startCond);
    cmd->startCond = 0;

    return 0;
}


/*
 *  Executed by the child process
 */
static void cmdTaskEntry(char *program, MprCmdTaskFn entry, int cmdArg)
{
    MprCmd          *cmd;
    MprCmdFile      *files;
    WIND_TCB        *tcb;
    char            **ep, *dir;
    int             inFd, outFd, errFd, id, rc;

    cmd = (MprCmd*) cmdArg;

    /*
     *  Open standard I/O files (in/out are from the server's perspective)
     */
    files = cmd->files;
    inFd = open(files[MPR_CMD_STDIN].name, O_RDONLY, 0666);
    outFd = open(files[MPR_CMD_STDOUT].name, O_WRONLY, 0666);
    errFd = open(files[MPR_CMD_STDERR].name, O_WRONLY, 0666);

    if (inFd < 0 || outFd < 0 || errFd < 0) {
        exit(255);
    }

    id = taskIdSelf();
    ioTaskStdSet(id, 0, inFd);
    ioTaskStdSet(id, 1, outFd);
    ioTaskStdSet(id, 2, errFd);

    /*
     *  Now that we have opened the stdin and stdout, wakeup our parent.
     */
    semGive(cmd->startCond);

    /*
     *  Create the environment
     */
    if (envPrivateCreate(id, -1) < 0) {
        exit(254);
    }
    for (ep = cmd->env; ep && *ep; ep++) {
        putenv(*ep);
    }

    /*
     *  Set current directory if required
     */
    if (cmd->dir) {
        rc = chdir(cmd->dir);
    } else {
        dir = mprGetPathDir(cmd, cmd->program);
        rc = chdir(dir);
        mprFree(dir);
    }
    if (rc < 0) {
        mprLog(cmd, 0, "cmd: Can't change directory to %s", cmd->dir);
        exit(255);
    }

    /*
     *  Call the user's entry point
     */
    (entry)(cmd->argc, cmd->argv, cmd->env);

    tcb = taskTcb(id);
    cmd->status = tcb->exitCode;

    /*
     *  Cleanup
     */
    envPrivateDestroy(id);
    close(inFd);
    close(outFd);
    close(errFd);
    semGive(cmd->exitCond);
}


static int makeChannel(MprCmd *cmd, int index)
{
    MprCmdFile      *file;
    static int      tempSeed = 0;

    file = &cmd->files[index];

    file->name = mprAsprintf(cmd, -1, "/pipe/%s_%d_%d", BLD_PRODUCT, taskIdSelf(), tempSeed++);

    if (pipeDevCreate(file->name, 5, MPR_BUFSIZE) < 0) {
        mprError(cmd, "Can't create pipes to run %s", cmd->program);
        return MPR_ERR_CANT_OPEN;
    }
    
    /*
     *  Open the server end of the pipe. MPR_CMD_STDIN is from the client's perspective.
     */
    if (index == MPR_CMD_STDIN) {
        file->fd = open(file->name, O_WRONLY, 0644);
    } else {
        file->fd = open(file->name, O_RDONLY, 0644);
    }
    if (file->fd < 0) {
        mprError(cmd, "Can't create stdio pipes. Err %d", mprGetOsError());
        return MPR_ERR_CANT_CREATE;
    }
    return 0;
}
#endif /* VXWORKS */


static void closeFiles(MprCmd *cmd)
{
    int     i;
    for (i = 3; i < MPR_MAX_FILE; i++) {
        close(i);
    }
}


#if BLD_UNIX_LIKE
/*
 *  CYGWIN requires a PATH or else execve hangs in cygwin 1.7
 */
static char **fixenv(MprCmd *cmd)
{
    char    **env;

    env = cmd->env;
#if CYGWIN
    if (env) {
        int     i, envc;

        for (envc = 0; cmd->env[envc]; envc++) {
            if (strstr(cmd->env[envc], "PATH=") != 0) {
                return cmd->env;
            }
        }
        if ((env = mprAlloc(cmd, sizeof(void*) * (envc + 2))) == NULL) {
            return NULL;
        }
        i = 0;
        env[i++] = mprStrcat(cmd, -1, "PATH=", getenv("PATH"), NULL);
        for (envc = 0; cmd->env[envc]; envc++) {
            env[i++] = cmd->env[envc];
        }
        env[i++] = 0;
    }
#endif /* CYGWIN */
    return env;
}
#endif


#else /* BLD_FEATURE_CMD */
void __mprCmdDummy() {}
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
