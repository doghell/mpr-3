/**
 *  mprLock.c - Thread Locking Support
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/*********************************** Includes *********************************/

#include    "mpr.h"

#if BLD_FEATURE_MULTITHREAD
/***************************** Forward Declarations ***************************/

static int destroyLock(MprMutex *lock);
static int destroySpinLock(MprSpin *lock);

/************************************ Code ************************************/

MprMutex *mprCreateLock(MprCtx ctx)
{
    MprMutex    *lock;

    mprAssert(ctx);

    lock = mprAllocObjWithDestructor(ctx, MprMutex, destroyLock);
    if (lock == 0) {
        return 0;
    }

#if BLD_UNIX_LIKE
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&lock->cs, &attr);
    pthread_mutexattr_destroy(&attr);

#elif WINCE
    InitializeCriticalSection(&lock->cs);

#elif BLD_WIN_LIKE
    InitializeCriticalSectionAndSpinCount(&lock->cs, 5000);

#elif VXWORKS
    /* Removed SEM_INVERSION_SAFE */
    lock->cs = semMCreate(SEM_Q_PRIORITY | SEM_DELETE_SAFE);
    if (lock->cs == 0) {
        mprAssert(0);
        mprFree(lock);
        return 0;
    }
#endif
    return lock;
}


MprMutex *mprInitLock(MprCtx ctx, MprMutex *lock)
{
    mprAssert(ctx);

#if BLD_UNIX_LIKE
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&lock->cs, &attr);
    pthread_mutexattr_destroy(&attr);

#elif WINCE
    InitializeCriticalSection(&lock->cs);

#elif BLD_WIN_LIKE
    InitializeCriticalSectionAndSpinCount(&lock->cs, 5000);

#elif VXWORKS
    /* Removed SEM_INVERSION_SAFE */
    lock->cs = semMCreate(SEM_Q_PRIORITY | SEM_DELETE_SAFE);
    if (lock->cs == 0) {
        mprAssert(0);
        mprFree(lock);
        return 0;
    }
#endif
    return lock;
}


/*
 *  Destroy a lock. Must be locked on entrance.
 */
static int destroyLock(MprMutex *lock)
{
    mprAssert(lock);
#if BLD_UNIX_LIKE
    pthread_mutex_unlock(&lock->cs);
    pthread_mutex_destroy(&lock->cs);

#elif BLD_WIN_LIKE
    DeleteCriticalSection(&lock->cs);

#elif VXWORKS
    semDelete(lock->cs);
#endif
    return 0;
}


/*
 *  Try to attain a lock. Do not block! Returns true if the lock was attained.
 */
bool mprTryLock(MprMutex *lock)
{
    int     rc;
#if BLD_UNIX_LIKE
    rc = pthread_mutex_trylock(&lock->cs) != 0;

#elif BLD_WIN_LIKE
    rc = TryEnterCriticalSection(&lock->cs) == 0;

#elif VXWORKS
    rc = semTake(lock->cs, NO_WAIT) != OK;
#endif
    return (rc) ? 0 : 1;
}


MprSpin *mprCreateSpinLock(MprCtx ctx)
{
    MprSpin    *lock;

    mprAssert(ctx);

    lock = mprAllocObjWithDestructor(ctx, MprSpin, destroySpinLock);
    if (lock == 0) {
        return 0;
    }

#if USE_MPR_LOCK
    mprInitLock(ctx, &lock->cs);

#elif MACOSX
    lock->cs = OS_SPINLOCK_INIT;

#elif BLD_UNIX_LIKE && BLD_HAS_SPINLOCK
    pthread_spin_init(&lock->cs, 0);

#elif BLD_UNIX_LIKE
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&lock->cs, &attr);
    pthread_mutexattr_destroy(&attr);

#elif WINCE
    InitializeCriticalSection(&lock->cs);

#elif BLD_WIN_LIKE
    InitializeCriticalSectionAndSpinCount(&lock->cs, 5000);

#elif VXWORKS
    /* Removed SEM_INVERSION_SAFE */
    lock->cs = semMCreate(SEM_Q_PRIORITY | SEM_DELETE_SAFE);
    if (lock->cs == 0) {
        mprAssert(0);
        mprFree(lock);
        return 0;
    }
#endif

#if BLD_DEBUG
    lock->owner = 0;
#endif
    return lock;
}


/*
 *  Static version just for mprAlloc which needs locks that don't allocate memory.
 */
MprSpin *mprInitSpinLock(MprCtx ctx, MprSpin *lock)
{
    mprAssert(ctx);

#if USE_MPR_LOCK
    mprInitLock(ctx, &lock->cs);

#elif MACOSX
    lock->cs = OS_SPINLOCK_INIT;

#elif BLD_UNIX_LIKE && BLD_HAS_SPINLOCK
    pthread_spin_init(&lock->cs, 0);

#elif BLD_UNIX_LIKE
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
    pthread_mutex_init(&lock->cs, &attr);
    pthread_mutexattr_destroy(&attr);

#elif WINCE
    InitializeCriticalSection(&lock->cs);

#elif BLD_WIN_LIKE
    InitializeCriticalSectionAndSpinCount(&lock->cs, 5000);

#elif VXWORKS
    /* Removed SEM_INVERSION_SAFE */
    lock->cs = semMCreate(SEM_Q_PRIORITY | SEM_DELETE_SAFE);
    if (lock->cs == 0) {
        mprAssert(0);
        mprFree(lock);
        return 0;
    }
#endif

#if BLD_DEBUG
    lock->owner = 0;
#endif
    return lock;
}


/*
 *  Destroy a lock. Must be locked on entrance.
 */
static int destroySpinLock(MprSpin *lock)
{
    mprAssert(lock);
#if USE_MPR_LOCK || MACOSX
    ;

#elif BLD_UNIX_LIKE && BLD_HAS_SPINLOCK
    pthread_spin_destroy(&lock->cs);

#elif BLD_UNIX_LIKE
    pthread_mutex_destroy(&lock->cs);

#elif BLD_WIN_LIKE
    DeleteCriticalSection(&lock->cs);

#elif VXWORKS
    semDelete(lock->cs);
#endif
    return 0;
}




/*
 *  Try to attain a lock. Do not block! Returns true if the lock was attained.
 */
bool mprTrySpinLock(MprSpin *lock)
{
    int     rc;

#if USE_MPR_LOCK
    mprTryLock(&lock->cs);

#elif MACOSX
    rc = !OSSpinLockTry(&lock->cs);

#elif BLD_UNIX_LIKE && BLD_HAS_SPINLOCK
    rc = pthread_spin_trylock(&lock->cs) != 0;

#elif BLD_UNIX_LIKE
    rc = pthread_mutex_trylock(&lock->cs) != 0;

#elif BLD_WIN_LIKE
    rc = TryEnterCriticalSection(&lock->cs) == 0;

#elif VXWORKS
    rc = semTake(lock->cs, NO_WAIT) != OK;

#endif
#if BLD_DEBUG
    if (rc == 0) {
        mprAssert(lock->owner != mprGetCurrentOsThread());
        lock->owner = mprGetCurrentOsThread();
    }
#endif
    return (rc) ? 0 : 1;
}


/*
 *  Big global lock. Avoid using this.
 */
void mprGlobalLock(MprCtx ctx)
{
    Mpr *mpr;

    mpr = mprGetMpr(ctx);
    mprAssert(mpr);

    if (mpr && mpr->mutex) {
        mprLock(mpr->mutex);
    }
}


void mprGlobalUnlock(MprCtx ctx)
{
    Mpr *mpr;

    mpr = mprGetMpr(ctx);
    mprAssert(mpr);

    if (mpr && mpr->mutex) {
        mprUnlock(mpr->mutex);
    }
}


#if BLD_USE_LOCK_MACROS
/*
 *  Still define these even if using macros to make linking with *.def export files easier
 */
#undef mprLock
#undef mprUnlock
#undef mprSpinLock
#undef mprSpinUnlock
#endif

/*
 *  Lock a mutex
 */
void mprLock(MprMutex *lock)
{
#if BLD_UNIX_LIKE
    pthread_mutex_lock(&lock->cs);

#elif BLD_WIN_LIKE
    EnterCriticalSection(&lock->cs);

#elif VXWORKS
    semTake(lock->cs, WAIT_FOREVER);
#endif
}


void mprUnlock(MprMutex *lock)
{
#if BLD_UNIX_LIKE
    pthread_mutex_unlock(&lock->cs);

#elif BLD_WIN_LIKE
    LeaveCriticalSection(&lock->cs);

#elif VXWORKS
    semGive(lock->cs);
#endif
}


/*
 *  Use functions for debug mode. Production release uses macros
 */
/*
 *  Lock a mutex
 */
void mprSpinLock(MprSpin *lock)
{
#if BLD_DEBUG
    /*
     *  Spin locks don't support recursive locking on all operating systems.
     */
    mprAssert(lock->owner != mprGetCurrentOsThread());
#endif

#if USE_MPR_LOCK
    mprLock(&lock->cs);

#elif MACOSX
    OSSpinLockLock(&lock->cs);

#elif BLD_UNIX_LIKE && BLD_HAS_SPINLOCK
    pthread_spin_lock(&lock->cs);

#elif BLD_UNIX_LIKE
    pthread_mutex_lock(&lock->cs);

#elif BLD_WIN_LIKE
    EnterCriticalSection(&lock->cs);

#elif VXWORKS
    semTake(lock->cs, WAIT_FOREVER);
#endif

#if BLD_DEBUG
    mprAssert(lock->owner != mprGetCurrentOsThread());
    lock->owner = mprGetCurrentOsThread();
#endif
}


void mprSpinUnlock(MprSpin *lock)
{
#if BLD_DEBUG
    lock->owner = 0;
#endif

#if USE_MPR_LOCK
    mprUnlock(&lock->cs);

#elif MACOSX
    OSSpinLockUnlock(&lock->cs);

#elif BLD_UNIX_LIKE && BLD_HAS_SPINLOCK
    pthread_spin_unlock(&lock->cs);

#elif BLD_UNIX_LIKE
    pthread_mutex_unlock(&lock->cs);

#elif BLD_WIN_LIKE
    LeaveCriticalSection(&lock->cs);

#elif VXWORKS
    semGive(lock->cs);
#endif
}


#else /* BLD_FEATURE_MULTITHREAD */
void __dummyMprLock() {}
#endif /* BLD_FEATURE_MULTITHREAD */

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
