/**
 *  mprCond.c - Thread Conditional variables
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

#include    "mpr.h"

/***************************** Forward Declarations ***************************/

static int condDestructor(MprCond *cp);
static int waitWithService(MprCond *cp, int timeout);

/************************************ Code ************************************/
/*
 *  Create a condition variable for use by single or multiple waiters
 */

MprCond *mprCreateCond(MprCtx ctx)
{
    MprCond     *cp;

    cp = mprAllocObjWithDestructor(ctx, MprCond, condDestructor);
    if (cp == 0) {
        return 0;
    }
    cp->triggered = 0;
#if BLD_FEATURE_MULTITHREAD
    cp->mutex = mprCreateLock(cp);

#if BLD_WIN_LIKE
    cp->cv = CreateEvent(NULL, FALSE, FALSE, NULL);
#elif VXWORKS
    cp->cv = semCCreate(SEM_Q_PRIORITY, SEM_EMPTY);
#else
    pthread_cond_init(&cp->cv, NULL);
#endif
#endif

    return cp;
}


/*
 *  Condition variable destructor
 */
static int condDestructor(MprCond *cp)
{
    mprAssert(cp);
    
#if BLD_FEATURE_MULTITHREAD
    mprAssert(cp->mutex);
    mprLock(cp->mutex);

#if BLD_WIN_LIKE
    CloseHandle(cp->cv);
#elif VXWORKS
    semDelete(cp->cv);
#else
    pthread_cond_destroy(&cp->cv);
#endif
    /* mprFree will call the mutex lock destructor */
#endif
    return 0;
}


#if BLD_FEATURE_MULTITHREAD
/*
 *  Wait for the event to be triggered. Should only be used when there are single waiters. If the event is already
 *  triggered, then it will return immediately. Timeout of -1 means wait forever. Timeout of 0 means no wait.
 *  Returns 0 if the event was signalled. Returns < 0 if the timeout.
 */
int mprWaitForCond(MprCond *cp, int timeout)
{
    MprTime     now, expire;
    int         rc;
#if BLD_UNIX_LIKE
    struct timespec     waitTill;
    struct timeval      current;
    int                 usec;
#endif

    rc = 0;
    if (timeout < 0) {
        timeout = MAXINT;
    }
    now = mprGetTime(cp);
    expire = now + timeout;

#if BLD_UNIX_LIKE
        gettimeofday(&current, NULL);
        usec = current.tv_usec + (timeout % 1000) * 1000;
        waitTill.tv_sec = current.tv_sec + (timeout / 1000) + (usec / 1000000);
        waitTill.tv_nsec = (usec % 1000000) * 1000;
#endif

    mprLock(cp->mutex);
    if (!cp->triggered) {
        /*
         *  WARNING: Can get spurious wakeups on some platforms (Unix + pthreads). 
         */
        do {
#if BLD_WIN_LIKE
            mprUnlock(cp->mutex);
            rc = WaitForSingleObject(cp->cv, (int) (expire - now));
            mprLock(cp->mutex);
            if (rc == WAIT_OBJECT_0) {
                rc = 0;
                ResetEvent(cp->cv);
            } else if (rc == WAIT_TIMEOUT) {
                rc = MPR_ERR_TIMEOUT;
            } else {
                rc = MPR_ERR_GENERAL;
            }
#elif VXWORKS
            mprUnlock(cp->mutex);
            rc = semTake(cp->cv, (int) (expire - now));
            mprLock(cp->mutex);
            if (rc != 0) {
                if (errno == S_objLib_OBJ_UNAVAILABLE) {
                    rc = MPR_ERR_TIMEOUT;
                } else {
                    rc = MPR_ERR_GENERAL;
                }
            }
            
#elif BLD_UNIX_LIKE
            /*
             *  NOTE: pthread_cond_timedwait can return 0 (MAC OS X and Linux). The pthread_cond_wait routines will 
             *  atomically unlock the mutex before sleeping and will relock on awakening.  
             */
            rc = pthread_cond_timedwait(&cp->cv, &cp->mutex->cs,  &waitTill);
            if (rc == ETIMEDOUT) {
                rc = MPR_ERR_TIMEOUT;
            } else if (rc != 0) {
                mprAssert(rc == 0);
                rc = MPR_ERR_GENERAL;
            }
#endif
        } while (!cp->triggered && rc == 0 && (now = mprGetTime(cp)) < expire);
    }

    if (cp->triggered) {
        cp->triggered = 0;
        rc = 0;
    } else if (rc == 0) {
        rc = MPR_ERR_TIMEOUT;
    }
    mprUnlock(cp->mutex);
    return rc;
}


/*
 *  Signal a condition and wakeup the waiter. Note: this may be called prior to the waiter waiting.
 */
void mprSignalCond(MprCond *cp)
{
    mprLock(cp->mutex);
    if (!cp->triggered) {
        cp->triggered = 1;
#if BLD_WIN_LIKE
        SetEvent(cp->cv);
#elif VXWORKS
        semGive(cp->cv);
#else
        pthread_cond_signal(&cp->cv);
#endif
    }
    mprUnlock(cp->mutex);
}


void mprResetCond(MprCond *cp)
{
    mprLock(cp->mutex);
    cp->triggered = 0;
#if BLD_WIN_LIKE
    ResetEvent(cp->cv);
#elif VXWORKS
    semDelete(cp->cv);
    cp->cv = semCCreate(SEM_Q_PRIORITY, SEM_EMPTY);
#else
    pthread_cond_destroy(&cp->cv);
    pthread_cond_init(&cp->cv, NULL);
#endif
    mprUnlock(cp->mutex);
}

#else /* BLD_FEATURE_MULTITHREAD */

/*
 *  Single threaded versions
 */
void mprSignalCond(MprCond *cp)
{
    cp->triggered = 1;
}


int mprWaitForCond(MprCond *cp, int timeout)
{
    return waitWithService(cp, timeout);
}


void mprResetCond(MprCond *cp)
{
    cp->triggered = 0;
}

#endif /* Single threaded */


/*
 *  Wait for an event to be triggered. Service events while waiting. Return 0 if triggered, < 0 on timeout.
 */
static int waitWithService(MprCond *cp, int timeout)
{
    MprTime     mark;

    if (timeout < 0) {
        timeout = MAXINT;
    }
    mark = mprGetTime(cp);
    while (!cp->triggered) {
        /*
         *  Must nap briefly incase another thread is the service thread and this thread is locked out
         */ 
        mprServiceEvents(mprGetDispatcher(cp), 10, MPR_SERVICE_IO | MPR_SERVICE_EVENTS | MPR_SERVICE_ONE_THING);
        if (mprGetElapsedTime(cp, mark) > timeout) {
            if (cp->triggered) {
                break;
            }
            return MPR_ERR_TIMEOUT;
        }
    }
    cp->triggered = 0;
    return 0;
}


/*
 *  Wait for a condition to be true and service events if required. This routine is required if single-threaded or
 *  if multi-threaded and there there is no service thread (or this thread is the service thread).
 */
int mprWaitForCondWithService(MprCond *cp, int timeout)
{
#if BLD_FEATURE_MULTITHREAD
    /*
     *  If we must wake a dispatcher, then it is safe to sleep as we are not the dispatcher.
     */
    if (mprMustWakeDispatcher(cp)) {
        return mprWaitForCond(cp, timeout);
    }
#endif
    return waitWithService(cp, timeout);
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
