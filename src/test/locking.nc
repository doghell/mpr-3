/*
 *  Test harness to benchmark multi-cpu locking overhead
 */
#include "mpr.h"

#if BLD_FEATURE_MULTITHREAD

/*
 *  Different styles of locking
 */
#define SPIN        1           /* Use spin locks */
#define MPR         2           /* Use MPR locks */
#define PTHREAD     3           /* Use pure pthreads */
#define NO          4           /* No locks whatsoever */

#define ITER_FACTOR (1024 * 20)

/*
 *  What kind of locking to use
 */
int lockStyle = MPR;

/*
 *  Use the same lock. This caused (desired) thread contention.
 */
int same = 1;

/*
 *  Some shared data
 */
intptr_t shared;

/*
 *  Count of threads to use
 */
int threadCount;

/*
 *  Test iterations
 */
int iterations;

/*
 *  Inner work counter
 */
int workCounter = 50;

MprMutex *globalMutex;

void lock(void *m)
{
    if (lockStyle == SPIN) {
        #if BLD_WIN_LIKE
            mprLock((MprMutex*) m);
        #elif LINUX
            pthread_spin_lock((pthread_spinlock_t*) m);
        #elif MACOSX
            OSSpinLockLock((OSSpinLock*) m);
        #endif
    } else if (lockStyle == MPR) {
        mprLock((MprMutex*) m);

#if BLD_UNIX_LIKE
    } else if (lockStyle == PTHREAD) {
        pthread_mutex_lock((pthread_mutex_t*) m);
#endif
    }
}


void unlock(void *m)
{
    if (lockStyle == SPIN) {
        #if BLD_WIN_LIKE
            mprUnlock((MprMutex*) m);
        #elif LINUX
            pthread_spin_unlock((pthread_spinlock_t*) m);
        #elif MACOSX
            OSSpinLockUnlock((OSSpinLock*) m);
        #endif
    } else if (lockStyle == MPR) {
        mprUnlock((MprMutex*) m);

#if BLD_UNIX_LIKE
    } else if (lockStyle == PTHREAD) {
        pthread_mutex_unlock((pthread_mutex_t*) m);
#endif
    }
}


/*
 *  Work functions
 */
int fun(int x, int y) {
    return x + y * y;
}

int work(MprThread *tp) {
    volatile int        j, i, z;

    z = 0;
    j = 10;
    for (i = 0; i < workCounter; i++) {
        z += fun(i, j);
        shared = (intptr_t) tp;
        if (shared != (intptr_t) tp && same && lockStyle != NO) {
            printf("CORRUPTION\n");
            exit(255);
        }
    }
    mprAssert(i == 500);
    return z;
}

void threadWorker(void *mutex, MprThread *tp)
{
    int     i;

    for (i = 0; i < iterations; i++) {
        lock(mutex);
        work(tp);
        unlock(mutex);
    }

    mprLock(globalMutex);
    if (--threadCount == 0) {
        exit(0);
    }
    mprUnlock(globalMutex);
}



int main(int argc, char **argv)
{
    MprThread   *tp;
    Mpr         *mpr;
    static void *locks[128];
    int         i, index;

    mpr = mprCreate(argc, argv, 0);
    mprStart(mpr, 0);

    if (argc != 5 && argc != 6) {
        printf("Usage: bench lockStyle=[mpr,spin,no] threads iterations workCounter [isolated]\n");
        exit(1);
    }

    if (strcmp(argv[1], "mpr") == 0) {
        lockStyle = MPR;
    } else if (strcmp(argv[1], "spin") == 0) {
        lockStyle = SPIN;
    } else if (strcmp(argv[1], "pthread") == 0) {
        lockStyle = PTHREAD;
    } else {
        lockStyle = NO;
        argv[1] = "no";
    }
    threadCount = atoi(argv[2]);
    iterations = atoi(argv[3]) * ITER_FACTOR;
    workCounter = atoi(argv[4]);
    if (argc == 6 && strcmp(argv[5], "isolated") == 0) {
        same = 0;
    }

    globalMutex = mprCreateLock(mpr);

    printf("\nUsing %s locking, %d threads, %d iterations, workCounter %d, %s, %d locks\n", 
        argv[1], threadCount, iterations / ITER_FACTOR, workCounter, same ? "with contention" : "no contention", iterations);

    /*
     *  Create the per thread locks. In the normal case (same), only the first lock is used. To measure the
     *  overhead of using the locks without any lock contention, set same on the command line.
     */
    if (lockStyle == SPIN) {
        for (i = 0; i < threadCount; i++) {
#if LINUX
            pthread_spinlock_t     *spin;
            spin = locks[i] = mprAllocZeroed(mpr, sizeof(pthread_spinlock_t));
            pthread_spin_init(spin, 0);

#elif MACOSX
            locks[i] = OS_SPINLOCK_INIT;
#endif
        }

    } else if (lockStyle == MPR) {
        for (i = 0; i < threadCount; i++) {
            locks[i] = mprCreateLock(mpr);
        }

#if BLD_UNIX_LIKE
    } else if (lockStyle == PTHREAD) {
        for (i = 0; i < threadCount; i++) {
            pthread_mutex_t     *mutex;
            pthread_mutexattr_t attr;
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE_NP);
            mutex = locks[i] = mprAllocZeroed(mpr, sizeof(pthread_mutex_t));
            pthread_mutex_init(mutex, &attr);
            pthread_mutexattr_destroy(&attr);
        }
#endif
    }

    for (i = 0; i < threadCount; i++) {
        index = (same) ? 0: i;

        if (lockStyle == SPIN) {
            tp = mprCreateThread(mpr, "t", (MprThreadProc) threadWorker, (void*) &locks[index], 0, 0);

        } else if (lockStyle == MPR) {
            tp = mprCreateThread(mpr, "t", (MprThreadProc) threadWorker, (void*) locks[index], 0, 0);

        } else if (lockStyle == PTHREAD) {
            tp = mprCreateThread(mpr, "t", (MprThreadProc) threadWorker, (void*) locks[index], 0, 0);

        } else {
            tp = mprCreateThread(mpr, "t", (MprThreadProc) threadWorker, (void*) NULL, 0, 0);
        }
        mprStartThread(tp);
    }

    mprSleep(mpr, 9999 * 1000);

    return 0;
}

#else
int main() {
    printf("Requires configure --enable-multithread\n");
    return 0;
}
#endif
