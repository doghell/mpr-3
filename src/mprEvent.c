/*
 *  mprEvent.c - Event queue and event service
 *
 *  This module is thread-safe.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/********************************** Includes **********************************/

#include    "mpr.h"

/***************************** Forward Declarations ***************************/

static void appendEvent(MprEvent *prior, MprEvent *event);
static int  eventDestructor(MprEvent *event);
static void queueEvent(MprDispatcher *es, MprEvent *event);
static void removeEvent(MprEvent *event);

/************************************* Code ***********************************/
/*
 *  Initialize the event service.
 */
MprDispatcher *mprCreateDispatcher(MprCtx ctx)
{
    MprDispatcher   *dispatcher;

    dispatcher = mprAllocObjWithDestructorZeroed(ctx, MprDispatcher, eventDestructor);
    if (dispatcher == 0) {
        return 0;
    }

#if BLD_FEATURE_MULTITHREAD
    dispatcher->mutex = mprCreateLock(dispatcher);
    dispatcher->spin = mprCreateSpinLock(dispatcher);
    dispatcher->cond = mprCreateCond(dispatcher);
    if (dispatcher->mutex == 0 || dispatcher->spin == 0 || dispatcher->cond == 0) {
        mprFree(dispatcher);
        return 0;
    }
#endif
    dispatcher->eventQ.next = &dispatcher->eventQ;
    dispatcher->eventQ.prev = &dispatcher->eventQ;
    dispatcher->timerQ.next = &dispatcher->timerQ;
    dispatcher->timerQ.prev = &dispatcher->timerQ;
    dispatcher->now = mprGetTime(ctx);
    return dispatcher;
}


MprDispatcher *mprGetDispatcher(MprCtx ctx)
{
    return mprGetMpr(ctx)->dispatcher;
}


/*
 *  Queue a new event for service according to its priority and position in the event queue. Period is used as 
 *  the delay before running the event and as the period between events for continuous events.
 */
MprEvent *mprCreateEvent(MprDispatcher *dispatcher, MprEventProc proc, int period, int priority, void *data, int flags)
{
    MprEvent        *event;

    event = mprAllocObjWithDestructor(dispatcher, MprEvent, eventDestructor);
    if (event == 0) {
        return 0;
    }
    event->proc = proc;
    event->period = period;
    event->priority = priority;
    event->data = data;
    event->flags = flags;
    event->timestamp = dispatcher->now;
    event->due = event->timestamp + period;
    event->dispatcher = dispatcher;

    /*
     *  Append in delay and priority order
     */
    queueEvent(dispatcher, event);
    mprWakeDispatcher(dispatcher);
    return event;
}


/*
 *  Called in response to mprFree on the event service
 */
static int eventDestructor(MprEvent *event)
{
    mprAssert(event);

    if (event->next) {
        mprRemoveEvent(event);
    }
    return 0;
}


/*  
 *  Remove an event from the event queues. Use mprRescheduleEvent to restart.
 */
void mprRemoveEvent(MprEvent *event)
{
    MprDispatcher   *dispatcher;
    Mpr             *mpr;

    mpr = mprGetMpr(event);
    dispatcher = mpr->dispatcher;

    mprSpinLock(dispatcher->spin);
    removeEvent(event);
    if (dispatcher->timerQ.next != &dispatcher->timerQ) {
        dispatcher->lastEventDue = dispatcher->timerQ.prev->due;
    } else {
        dispatcher->lastEventDue = dispatcher->now;
    }
    mprSpinUnlock(dispatcher->spin);
}


void mprStopContinuousEvent(MprEvent *event)
{
    event->flags &= ~MPR_EVENT_CONTINUOUS;
}


void mprRestartContinuousEvent(MprEvent *event)
{
    event->flags |= MPR_EVENT_CONTINUOUS;
    mprRescheduleEvent(event, event->period);
}


/*
 *  Internal routine to queue an event to the event queue in delay and priority order. 
 */
static void queueEvent(MprDispatcher *dispatcher, MprEvent *event)
{
    MprEvent    *prior, *q;

    mprSpinLock(dispatcher->spin);
    if (event->due > dispatcher->now) {
        /*
         *  Due in the future some time
         */
        q = &dispatcher->timerQ;

        if (event->due > dispatcher->lastEventDue) {
            prior = q->prev;
            dispatcher->lastEventDue = event->due;

        } else {
            /*
             *  Scan backwards for the event just prior to this new event
             */
            for (prior = q->prev; prior != q; prior = prior->prev) {
                if (event->due > prior->due) {
                    break;
                } else if (event->due == prior->due && event->priority >= prior->priority) {
                    break;
                }
            }
        }
    } else {
        q = &dispatcher->eventQ;
        for (prior = q->prev; prior != q; prior = prior->prev) {
            if (event->due > prior->due) {
                break;
            } else if (event->due == prior->due && event->priority >= prior->priority) {
                break;
            }
        }
        dispatcher->eventCounter++;
    }

    /*
     *  Will assert if already in the queue
     */
    mprAssert(prior != event);
    appendEvent(prior, event);
    mprSpinUnlock(dispatcher->spin);
}


/*
 *  Get the next event from the front of the event queue
 *  Return 0 if not event.
 */
MprEvent *mprGetNextEvent(MprDispatcher *dispatcher)
{
    MprEvent    *event, *next;

    mprSpinLock(dispatcher->spin);
    event = dispatcher->eventQ.next;
    if (event != &dispatcher->eventQ) {
        removeEvent(event);

    } else {
        /*
         *  Move due timer events to the event queue. Allows priorities to work.
         */
        for (event = dispatcher->timerQ.next; event != &dispatcher->timerQ; event = next) {
            if (event->due > dispatcher->now) {
                break;
            }
            next = event->next;
            removeEvent(event);
            appendEvent(&dispatcher->eventQ, event);
            dispatcher->eventCounter++;
        }
            
        event = dispatcher->eventQ.next;
        if (event != &dispatcher->eventQ) {
            removeEvent(event);
        } else {
            event = 0;
        }
    }
    mprSpinUnlock(dispatcher->spin);
    return event;
}


void mprWakeDispatcher(MprDispatcher *dispatcher)
{
#if BLD_FEATURE_MULTITHREAD
    mprSpinLock(dispatcher->spin);
    if (dispatcher->flags & MPR_DISPATCHER_WAIT_EVENTS) {
        mprSignalCond(dispatcher->cond);
    }
    if (dispatcher->flags & MPR_DISPATCHER_WAIT_IO) {
        mprWakeWaitService(dispatcher);
    }
    mprSpinUnlock(dispatcher->spin);
#endif
}


/*
 *  Service events until the timeout expires or if MPR_SERVICE_ONE_THING is set, then until one event is received.
 */
int mprServiceEvents(MprDispatcher *dispatcher, MprTime timeout, int flags)
{
    MprTime     mark, remaining;
    MprEvent    *event;
    int         delay, total, rc;

    mprSpinLock(dispatcher->spin);
    if (flags & MPR_SERVICE_EVENTS) {
        dispatcher->flags |= MPR_DISPATCHER_WAIT_EVENTS;
    }
    if (flags & MPR_SERVICE_IO) {
        dispatcher->flags |= MPR_DISPATCHER_WAIT_IO;
    }
    mprSpinUnlock(dispatcher->spin);

    mark = dispatcher->now = mprGetTime(dispatcher);
    if (timeout < 0) {
        timeout = MAXINT64;
    }
    remaining = timeout;
    total = 0;

    do {
        if (flags & MPR_SERVICE_EVENTS) {
            if ((event = mprGetNextEvent(dispatcher)) != 0) {
                mprDoEvent(event, 0);
                total++;
                if (flags & MPR_SERVICE_ONE_THING) {
                    break;
                }
                continue;
            }
        } 
        if (mprIsComplete(dispatcher)) {
            break;
        }
        if (flags & MPR_SERVICE_IO) {
            dispatcher->now = mprGetTime(dispatcher);
            delay = mprGetIdleTime(dispatcher);
            delay = (int) min(remaining, delay);
            if ((rc = mprWaitForIO(mprGetMpr(dispatcher)->waitService, delay)) > 0) {
                total += rc;
            }
#if BLD_FEATURE_MULTITHREAD
        } else if (MPR_SERVICE_EVENTS && remaining > 0) {
            mprWaitForCond(dispatcher->cond, (int) remaining);
#endif
        }
        remaining = mprGetRemainingTime(dispatcher, mark, timeout);
    } while (remaining > 0 && !mprIsComplete(dispatcher) && !(flags & MPR_SERVICE_ONE_THING));

    mprSpinLock(dispatcher->spin);
    dispatcher->flags &= ~MPR_DISPATCHER_WAIT_IO;
    dispatcher->flags &= ~MPR_DISPATCHER_WAIT_EVENTS;
    mprSpinUnlock(dispatcher->spin);
    return total;
}


void mprDoEvent(MprEvent *event, void *workerThread)
{
    MprDispatcher   *dispatcher;

#if BLD_FEATURE_MULTITHREAD
    if (event->flags & MPR_EVENT_THREAD && workerThread == 0) {
        /*
         *  Recall mprDoEvent but via a worker thread. If none available, then handle inline.
         */
        if (mprStartWorker(event->dispatcher, (MprWorkerProc) mprDoEvent, (void*) event, event->priority) == 0) {
            return;
        }
    }
#endif
    /*
     *  If it is a continuous event, we requeue here so that the event callback has the option of deleting the event.
     */
    dispatcher = mprGetMpr(event)->dispatcher;
    if (event->flags & MPR_EVENT_CONTINUOUS) {
        event->timestamp = dispatcher->now;
        event->due = event->timestamp + event->period;
        queueEvent(dispatcher, event);
    }
    /*
     *  The callback can delete the event. NOTE: callback events MUST NEVER block.
     */
    if (event->proc) {
        mprSpinLock(dispatcher->spin);
        dispatcher->flags |= MPR_DISPATCHER_DO_EVENT;
        mprSpinUnlock(dispatcher->spin);

        (*event->proc)(event->data, event);

        mprSpinLock(dispatcher->spin);
        dispatcher->flags &= ~MPR_DISPATCHER_DO_EVENT;
        mprSpinUnlock(dispatcher->spin);
    }
}


/*
 *  Return the time till the next event.
 */
int mprGetIdleTime(MprDispatcher *dispatcher)
{
    int     delay;
    
    mprSpinLock(dispatcher->spin);
    if (dispatcher->eventQ.next != &dispatcher->eventQ) {
        delay = 0;
    } else if (dispatcher->timerQ.next != &dispatcher->timerQ) {
        delay = (int) (dispatcher->timerQ.next->due - dispatcher->now);
        if (delay < 0) {
            delay = 0;
        }
        
    } else {
        delay = INT_MAX;
    }
    mprSpinUnlock(dispatcher->spin);
    return delay;
}


int mprGetEventCounter(MprDispatcher *dispatcher)
{
    return dispatcher->eventCounter;
}


void mprRescheduleEvent(MprEvent *event, int period)
{
    MprDispatcher   *dispatcher;
    Mpr             *mpr;

    mpr = mprGetMpr(event);
    dispatcher = mprGetMpr(event)->dispatcher;

    event->period = period;
    event->timestamp = dispatcher->now;
    event->due = event->timestamp + period;

    if (event->next) {
        mprRemoveEvent(event);
    }
    queueEvent(mpr->dispatcher, event);
    mprWakeDispatcher(dispatcher);
}


MprEvent *mprCreateTimerEvent(MprDispatcher *dispatcher, MprEventProc proc, int period, int priority, void *data, int flags)
{
    return mprCreateEvent(dispatcher, proc, period, priority, data, MPR_EVENT_CONTINUOUS | flags);
}


/*
 *  Append a new event. Must be locked when called.
 */
static void appendEvent(MprEvent *prior, MprEvent *event)
{
    event->prev = prior;
    event->next = prior->next;
    prior->next->prev = event;
    prior->next = event;
}


/*
 *  Remove an event. Must be locked when called.
 */
static void removeEvent(MprEvent *event)
{
    event->next->prev = event->prev;
    event->prev->next = event->next;
    event->next = 0;
    event->prev = 0;
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
    vim: sw=4 ts=4 expandtab

    @end
 */
