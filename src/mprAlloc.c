/**
 *  mprAlloc.c - Memory Allocator. This is a layer above malloc providing memory services including: virtual memory mapping,
 *               slab based and arena based allocations.
 *
 *  Copyright (c) All Rights Reserved. See details at the end of the file.
 */

/******************************* Documentation ********************************/

/*
 *  This is a memory "turbo-charger" that sits above malloc. It provides arena and slab based allocations. The goal is
 *  to provide a scalable memory allocator that supports hierarchical allocations and performs well in multi-threaded apps.
 *  It suports arena-based and slab-based allocations.
 * 
 *  This module uses several preprocessor directives to control features:
 *
 *      BLD_FEATURE_MEMORY_DEBUG            Enable checks for block integrity. Fills blocks on allocation and free.
 *      BLD_FEATURE_MEMORY_STATS            Enables accumulation of memory stats.
 *      BLD_FEATURE_MONITOR_STACK           Monitor stack use
 *      BLD_FEATURE_MULTITHREAD             Defined if building a multi-threaded application.
 *      BLD_FEATURE_VERIFY                  Adds deep and slow integrity tests.
 *      BLD_CC_MMU                          Enabled if the system has a memory management unit supporting virtual memory.
 */

/********************************* Includes ***********************************/

#include    "mpr.h"

/******************************* Local Defines ********************************/
/*
 *  Convert from user pointers to memory blocks and back again.
 */
#define GET_BLK(ptr)            ((MprBlk*) (((char*) (ptr)) - MPR_ALLOC_HDR_SIZE))
#define GET_PTR(bp)             ((char*) (((char*) (bp)) + MPR_ALLOC_HDR_SIZE))
#define GET_USIZE(bp)           ((bp->size) - MPR_ALLOC_HDR_SIZE)
#define DESTRUCTOR_PTR(bp)      (((char*) bp) + bp->size - sizeof(MprDestructor))
#define GET_DESTRUCTOR(bp)      ((bp->flags & MPR_ALLOC_HAS_DESTRUCTOR) ? \
                                    (MprDestructor) (*(MprDestructor*) (DESTRUCTOR_PTR(bp))) : 0)
#define SET_DESTRUCTOR(bp, d)   if (d) { bp->flags |= MPR_ALLOC_HAS_DESTRUCTOR; \
                                    *((MprDestructor*) DESTRUCTOR_PTR(bp)) = d; } else
#if BLD_FEATURE_MEMORY_DEBUG
#define VALID_BLK(bp)           ((bp)->magic == MPR_ALLOC_MAGIC)
#define VALID_CTX(ptr)          (VALID_BLK(GET_BLK(ptr)))
#define SET_MAGIC(bp)           (bp)->magic = MPR_ALLOC_MAGIC

/*
 *  Set this address to break when this address is allocated or freed. This is a block address (not a user ptr).
 */
static MprBlk *stopAlloc;
static int stopSeqno = -1;
static int allocCount = 1;

#else
#define VALID_BLK(bp)           (1)
#define VALID_CTX(ptr)          (1)
#define SET_MAGIC(bp)
#endif

/*
 *  Heaps may be "thread-safe" such that lock and unlock requests on a single heap can come from different threads.
 *  The lock and unlock macros will use spin locks because we expect contention to be very low.
 */
#if BLD_FEATURE_MULTITHREAD
#define lockHeap(heap)               if (unlikely(heap->flags & MPR_ALLOC_THREAD_SAFE)) { mprSpinLock(&heap->spin); }
#define unlockHeap(heap)             if (unlikely(heap->flags & MPR_ALLOC_THREAD_SAFE)) { mprSpinUnlock(&heap->spin); }
#else
#define lockHeap(ctx)
#define unlockHeap(ctx)
#endif

#if BLD_HAS_GLOBAL_MPR || BLD_WIN_LIKE
/*
 *  Mpr control and root memory context. This is a constant and a permissible global.
 */
    #if BLD_WIN_LIKE
        static Mpr  *_globalMpr;
    #else
        #undef _globalMpr
        Mpr  *_globalMpr;
    #endif
#endif

/***************************** Forward Declarations ***************************/

static void allocException(MprBlk *bp, uint size, bool granted);
static void *allocMemory(uint size);
static void allocError(MprBlk *parent, uint size);
static void freeBlock(Mpr *mpr, MprHeap *heap, MprBlk *bp);
static void freeMemory(MprBlk *bp);
static void initHeap(MprHeap *heap, cchar *name, bool threadSafe);
static void linkBlock(MprBlk *parent, MprBlk *bp);
static void sysinit(Mpr *mpr);
static void unlinkBlock(MprBlk *bp);

#if BLD_CC_MMU
static MprRegion *createRegion(MprCtx ctx, MprHeap *heap, uint size);
#endif
#if BLD_FEATURE_MEMORY_STATS
static void incStats(MprHeap *heap, MprBlk *bp);
static void decStats(MprHeap *heap, MprBlk *bp);
#else
#define incStats(heap, parent, bp)
#define decStats(heap, bp)
#endif
#if BLD_FEATURE_MONITOR_STACK
static void monitorStack(Mpr *mpr);
#endif
#if BLD_WIN_LIKE
static int mapProt(int flags);
#endif

/************************************* Code ***********************************/
/*
 *  Initialize the memory subsystem
 */
Mpr *mprCreateAllocService(MprAllocNotifier cback, MprDestructor destructor)
{
    Mpr             *mpr;
    MprBlk          *bp;
    uint            usize, size;

    /*
     *  Hand-craft the first block to optimize subsequent use of mprAlloc. Layout is:
     *      HDR
     *      Mpr
     *          MprHeap
     *      Destructor
     */
    usize = sizeof(Mpr) + sizeof(MprDestructor);
    size = (uint) MPR_ALLOC_ALIGN(MPR_ALLOC_HDR_SIZE + usize);
    usize = (uint) (size - MPR_ALLOC_HDR_SIZE);

    bp = (MprBlk*) allocMemory(size);
    if (bp == 0) {
        if (cback) {
            (*cback)(0, sizeof(Mpr), 0, 0);
        }
        return 0;
    }
    memset(bp, 0, size);
    mpr = (Mpr*) GET_PTR(bp);

    /*
     *  Set initial defaults to no memory limit. Redline at 90%.
     */
    mpr->alloc.maxMemory = INT_MAX;
    mpr->alloc.redLine = INT_MAX / 100 * 99;

#if BLD_HAS_GLOBAL_MPR || BLD_WIN_LIKE
    _globalMpr = mpr;
#endif

    bp->parent = 0;
    bp->size = size;
    SET_DESTRUCTOR(bp, destructor);

    SET_MAGIC(bp);
    bp->flags |= MPR_ALLOC_IS_HEAP;

    mpr->alloc.bytesAllocated += size;
    mpr->alloc.peakAllocated = mpr->alloc.bytesAllocated;
    mpr->alloc.stackStart = (void*) &mpr;

    sysinit(mpr);
    initHeap(&mpr->pageHeap, "page", 1);
    mpr->pageHeap.flags = MPR_ALLOC_PAGE_HEAP | MPR_ALLOC_THREAD_SAFE;
    initHeap(&mpr->heap, "mpr", 1);

    mpr->heap.notifier = cback;
    mpr->heap.notifierCtx = mpr;

#if BLD_FEATURE_MEMORY_DEBUG
    stopAlloc = 0;
    stopSeqno = -1;
#endif
    return mpr;
}


static MprCtx allocHeap(MprCtx ctx, cchar *name, uint heapSize, bool threadSafe, MprDestructor destructor)
{
    Mpr         *mpr;
    MprHeap     *pageHeap, *heap;
    MprRegion   *region;
    MprBlk      *bp, *parent;
    int         headersSize, usize, size;

    mprAssert(ctx);
    mprAssert(VALID_CTX(ctx));

    mpr = mprGetMpr(ctx);

    /*
     *  Allocate the full arena/slab out of one memory allocation. This includes the user object, heap object and 
     *  heap memory. Do this because heaps should generally be initially sized to be sufficient for the apps needs 
     *  (they are virtual with MMUs)
     *
     *  Layout is:
     *      HDR
     *      MprHeap structure
     *      MprRegion structure
     *      Heap data (>= heapSize)
     *
     *  The MprHeap and MprRegion structures are aligned. This may result in the size allocated being bigger 
     *  than the requested heap size.
     */
    headersSize = MPR_ALLOC_ALIGN(sizeof(MprHeap) + sizeof(MprRegion));
    usize = headersSize + heapSize;
    size = (int) MPR_PAGE_ALIGN(MPR_ALLOC_HDR_SIZE + usize, mpr->alloc.pageSize);
    usize = (int) (size - MPR_ALLOC_HDR_SIZE);
    heapSize = usize - headersSize;

    parent = GET_BLK(ctx);
    mprAssert(parent);

    /*
     *  All heaps are allocated from the page heap
     */
    pageHeap = &mpr->pageHeap;
    mprAssert(pageHeap);

    if (unlikely((bp = _mprAllocBlock(ctx, pageHeap, NULL, usize)) == 0)) {
        allocError(parent, usize);
        unlockHeap(pageHeap);
        return 0;
    }

    lockHeap(pageHeap);
    bp->flags |= MPR_ALLOC_IS_HEAP;
    linkBlock(parent, bp);
    incStats(pageHeap, bp);
    unlockHeap(pageHeap);

    heap = (MprHeap*) GET_PTR(bp);
    heap->destructor = destructor;
    initHeap((MprHeap*) heap, name, threadSafe);

    region = (MprRegion*) ((char*) heap + sizeof(MprHeap));
    region->next = 0;
    region->memory = (char*) heap + headersSize;
    region->nextMem = region->memory;
    region->vmSize = MPR_ALLOC_ALIGN(MPR_ALLOC_HDR_SIZE + usize);
    region->size = heapSize;
    heap->region = region;
    return GET_PTR(bp);
}


/*
 *  Create an arena memory context. An arena context is a memory heap which allocates all child requests from a 
 *  single (logical) memory block. Allocations are done like simple salami slices. Arenas may be created thread-safe, 
 *  and are not thread-safe by default for speed.
 */
MprHeap *mprAllocArena(MprCtx ctx, cchar *name, uint arenaSize, bool threadSafe, MprDestructor destructor)
{
    MprHeap     *heap;

    mprAssert(ctx);
    mprAssert(VALID_CTX(ctx));
    mprAssert(arenaSize > 0);

    heap = (MprHeap*) allocHeap(ctx, name, arenaSize, threadSafe, destructor);
    if (heap == 0) {
        return 0;
    }
    heap->flags = MPR_ALLOC_ARENA_HEAP;
    return heap;
}


/*
 *  Create standard (malloc) heap. 
 */
MprHeap *mprAllocHeap(MprCtx ctx, cchar *name, uint arenaSize, bool threadSafe, MprDestructor destructor)
{
    MprHeap     *heap;

    mprAssert(ctx);
    mprAssert(VALID_CTX(ctx));
    mprAssert(arenaSize > 0);

    heap = (MprHeap*) allocHeap(ctx, name, arenaSize, threadSafe, destructor);
    if (heap == 0) {
        return 0;
    }
    heap->flags = MPR_ALLOC_MALLOC_HEAP;
    return heap;
}


/*
 *  Create an object slab memory context. An object slab context is a memory heap which allocates constant size objects 
 *  from a single (logical) memory block. The object slab keeps a free list of freed blocks. Object slabs may be created 
 *  thread-safe, but are thread insensitive by default and will allocate memory without any locking. Hence allocations 
 *  will be fast and scalable.
 *
 *  This call is typically made via the macro mprCreateSlab. ObjSize is the size of objects to create from the slab heap.
 *  The count parameter indicates how may objects the slab heap will initially contain. MaxCount is the the maximum the 
 *  heap will ever support. If maxCount is greater than count, then the slab is growable.
 *
 *  NOTE: Currently not being used
 */
MprHeap *mprAllocSlab(MprCtx ctx, cchar *name, uint objSize, uint count, bool threadSafe, MprDestructor destructor)
{
    MprHeap     *heap;
    uint        size;

    mprAssert(ctx);
    mprAssert(VALID_CTX(ctx));
    mprAssert(objSize > 0);
    mprAssert(count > 0);

    size = MPR_ALLOC_ALIGN(objSize) * count;
    heap = (MprHeap*) allocHeap(ctx, name, size, threadSafe, destructor);
    if (heap == 0) {
        return 0;
    }
    heap->flags = MPR_ALLOC_SLAB_HEAP;
    return heap;
}


/*
 *  Allocate a block. Not used to allocate heaps.
 */
void *_mprAlloc(MprCtx ctx, uint usize)
{
    MprBlk      *bp, *parent;
    MprHeap     *heap;

    mprAssert(ctx);
    mprAssert(usize >= 0);
    mprAssert(VALID_CTX(ctx));

    parent = GET_BLK(ctx);
    mprAssert(parent);
    heap = mprGetHeap(parent);
    mprAssert(heap);

    if (unlikely((bp = _mprAllocBlock(ctx, heap, parent, usize)) == 0)) {
        allocError(parent, usize);
        return 0;
    }
    return GET_PTR(bp);
}


/*
 *  Allocate and zero a block
 */
void *_mprAllocZeroed(MprCtx ctx, uint size)
{
    void    *newBlock;

    newBlock = _mprAlloc(ctx, size);
    mprAssert(newBlock);

    if (newBlock) {
        memset(newBlock, 0, size);
    }
    return newBlock;
}


/*
 *  Allocate an object. Typically used via the macro: mprAllocObj
 */
void *_mprAllocWithDestructor(MprCtx ctx, uint size, MprDestructor destructor)
{
    MprBlk      *bp;
    void        *ptr;

    mprAssert(VALID_CTX(ctx));
    mprAssert(size > 0);

    ptr = _mprAlloc(ctx, size + (int) sizeof(MprDestructor));
    mprAssert(ptr);
    if (ptr == 0) {
        return 0;
    }
    bp = GET_BLK(ptr);
    SET_DESTRUCTOR(bp, destructor);
    return ptr;
}


void mprSetDestructor(void *ptr, MprDestructor destructor)
{
    MprBlk      *bp;

    bp = GET_BLK(ptr);
    SET_DESTRUCTOR(bp, destructor);
}


#if BLD_DEBUG
cchar *mprGetName(void *ptr)
{
    MprBlk      *bp;

    bp = GET_BLK(ptr);
    return bp->name;
}


void *mprSetName(void *ptr, cchar *name)
{
    MprBlk      *bp;

    if (ptr) {
        bp = GET_BLK(ptr);
        if (bp) {
            bp->name = (char*) name;
        }
    }
    return ptr;
}


void *mprSetDynamicName(void *ptr, cchar *name)
{
    MprBlk      *bp;

    if (ptr) {
        bp = GET_BLK(ptr);
        if (bp) {
            bp->name = malloc(strlen(name) + 1);
            if (bp->name) {
                strcpy(bp->name, name);
            }
        }
    }
    return ptr;
}
#else
#undef mprSetName
#undef mprSetDynamicName
#undef mprGetName
void *mprSetName(void *ptr, cchar *name) { return ptr; }
void *mprSetDynamicName(void *ptr, cchar *name) { return ptr; }
cchar *mprGetName(void *ptr) { return ""; }
#endif


void mprInitBlock(MprCtx ctx, void *ptr, uint size)
{
    MprBlk      *bp;

    bp = GET_BLK(ptr);
    memset(ptr, 0, size);
    bp->parent = MPR_GET_BLK(mprGetMpr(ctx));
    bp->children = 0;
    bp->next = 0;
    bp->prev = 0;
    bp->size = 0;
    bp->flags = 0;
    SET_MAGIC(bp);
}


/*
 *  Allocate and zero a block
 */
void *_mprAllocWithDestructorZeroed(MprCtx ctx, uint size, MprDestructor destructor)
{
    void    *newBlock;

    newBlock = _mprAllocWithDestructor(ctx, size, destructor);
    if (newBlock) {
        memset(newBlock, 0, size);
    }
    return newBlock;
}


/*
 *  Free a block of memory. Free all children recursively. Return 0 if the memory was freed. A destructor may prevent
 *  memory being deleted by returning non-zero.
 */
int mprFree(void *ptr)
{
    Mpr         *mpr;
    MprHeap     *heap, *hp;
    MprBlk      *bp, *parent;

    if (unlikely(ptr == 0)) {
        return 0;
    }
    mpr = mprGetMpr(ptr);
    bp = GET_BLK(ptr);
    mprAssert(VALID_BLK(bp));
    mprAssert(bp->size > 0);

#if BLD_FEATURE_MEMORY_DEBUG
    if (bp == stopAlloc || bp->seqno == stopSeqno) {
        mprBreakpoint();
    }
    /*
     *  Test if already freed
     */
    if (unlikely(bp->parent == 0 && ptr != mpr)) {
        mprAssert(bp->parent);
        return 0;
    }
#endif

    /*
     *  We need to run destructors first if there is a destructor and it isn't a heap
     */
    if (unlikely(bp->flags & MPR_ALLOC_HAS_DESTRUCTOR)) {
        if ((GET_DESTRUCTOR(bp))(ptr) != 0) {
            /*
             *  Destructor aborted the free. Reparent to the top level.
             */
            mprStealBlock(mpr, ptr);
            return 1;
        }
    }
    
    mprFreeChildren(ptr);
    parent = bp->parent;

    if (unlikely(bp->flags & MPR_ALLOC_IS_HEAP)) {
        hp = (MprHeap*) ptr;
        if (hp->destructor) {
            hp->destructor(ptr);
        }
        heap = &mpr->pageHeap;

    } else {
        mprAssert(VALID_BLK(parent));
        heap = mprGetHeap(parent);
        mprAssert(heap);
    }

    lockHeap(heap);
    decStats(heap, bp);
    unlinkBlock(bp);
    freeBlock(mpr, heap, bp);
    if (ptr != mpr) {
        unlockHeap(heap);
    }
    return 0;
}


/*
 *  Free the children of a block of memory
 */
void mprFreeChildren(MprCtx ptr)
{
    MprBlk      *bp, *child, *next;

    if (unlikely(ptr == 0)) {
        return;
    }

    bp = GET_BLK(ptr);
    mprAssert(VALID_BLK(bp));

    /*
     *  Free the children. They are linked in LIFO order. So free from the start and it will actually free in reverse order.
     *  ie. last allocated will be first freed.
     */
    if (likely((child = bp->children) != NULL)) {
        do {
            mprAssert(VALID_BLK(child));
            next = child->next;
            mprFree(GET_PTR(child));
        } while ((child = next) != 0);
        bp->children = 0;
    }
}


/*
 *  Rallocate a block
 */
void *_mprRealloc(MprCtx ctx, void *ptr, uint usize)
{
    MprHeap     *heap;
    MprBlk      *parent, *bp, *newbp, *child;
    Mpr         *mpr;
    void        *newPtr;

    mprAssert(VALID_CTX(ctx));
    mprAssert(usize > 0);

    if (ptr == 0) {
        return _mprAlloc(ctx, usize);
    }
    mpr = mprGetMpr(ctx);
    mprAssert(VALID_CTX(ptr));
    bp = GET_BLK(ptr);
    mprAssert(bp);
    mprAssert(bp->parent);

    if (usize < GET_USIZE(bp)) {
        return ptr;
    }
    parent = GET_BLK(ctx);
    mprAssert(parent);

    newPtr = _mprAlloc(ctx, usize);
    if (newPtr == 0) {
        return 0;
    }

    newbp = GET_BLK(newPtr);
    mprAssert(newbp->parent == parent);
    memcpy(GET_PTR(newbp), GET_PTR(bp), GET_USIZE(bp));

    heap = mprGetHeap(parent);
    mprAssert(heap);
    lockHeap(heap);

    /*
     *  Remove old block
     */
    decStats(heap, bp);
    unlinkBlock(bp);

    /*
     *  Fix the parent pointer of all children
     */
    for (child = bp->children; child; child = child->next) {
        child->parent = newbp;
    }
    newbp->children = bp->children;
    unlockHeap(heap);
    freeBlock(mpr, heap, bp);
    return newPtr;
}


static int getBlockSize(MprBlk *bp) 
{
    MprBlk      *child;
    int         size;
    
    size = bp->size;
    for (child = bp->children; child; child = child->next) {
        size += getBlockSize(child);
    }
    return size;
}


/*
 *  Steal a block from one context and insert in a new context. Ptr is inserted into the Ctx context.
 */
int mprStealBlock(MprCtx ctx, cvoid *ptr)
{
    MprHeap     *heap, *newHeap;
    MprBlk      *bp, *parent, *newParent;
    int         total;

    if (ptr == 0) {
        return 0;
    }
    mprAssert(VALID_CTX(ctx));
    mprAssert(VALID_CTX(ptr));
    bp = GET_BLK(ptr);

#if BLD_FEATURE_MEMORY_VERIFY
    /*
     *  Ensure bp is not a parent of the nominated context.
     */
    for (parent = GET_BLK(ctx); parent; parent = parent->parent) {
        mprAssert(parent != bp);
    }
#endif

    parent = bp->parent;
    mprAssert(VALID_BLK(parent));
    heap = mprGetHeap(parent);
    mprAssert(heap);

    newParent = GET_BLK(ctx);
    mprAssert(VALID_BLK(newParent));
    newHeap = mprGetHeap(newParent);
    mprAssert(newHeap);

    if (heap == newHeap) {
        lockHeap(heap);
        unlinkBlock(bp);
        linkBlock(newParent, bp);
        unlockHeap(heap);
    } else {
        lockHeap(heap);
#if BLD_FEATURE_MEMORY_STATS
        /* Remove all child blocks from the heap */
        total = getBlockSize(bp) - bp->size;
        heap->allocBytes -= total;
        newHeap->allocBytes += total;
#endif
        decStats(heap, bp);
        unlinkBlock(bp);
        unlockHeap(heap);

        lockHeap(newHeap);
        linkBlock(newParent, bp);
        incStats(newHeap, bp);
        unlockHeap(newHeap);
    }
    return 0;
}


/*
 *  Fast unlocked steal within a single heap. WARNING: no locking!
 */
void mprReparentBlock(MprCtx ctx, cvoid *ptr)
{
    MprBlk      *bp;

    bp = GET_BLK(ptr);
    unlinkBlock(bp);
    linkBlock(GET_BLK(ctx), bp);
}


char *_mprStrdup(MprCtx ctx, cchar *str)
{
    char    *newp;
    int     len;

    mprAssert(VALID_CTX(ctx));

    if (str == 0) {
        str = "";
    }
    len = (int) strlen(str) + 1;
    newp = (char*) _mprAlloc(ctx, len);
    if (newp) {
        memcpy(newp, str, len);
    }
    return newp;
}


char *_mprStrndup(MprCtx ctx, cchar *str, uint usize)
{
    char    *newp;
    uint    len;

    mprAssert(VALID_CTX(ctx));

    if (str == 0) {
        str = "";
    }
    len = (int) strlen(str) + 1;
    len = min(len, usize);
    newp = (char*) _mprAlloc(ctx, len);
    if (newp) {
        memcpy(newp, str, len);
    }
    return newp;
}


void *_mprMemdup(MprCtx ctx, cvoid *ptr, uint usize)
{
    char    *newp;

    mprAssert(VALID_CTX(ctx));

    newp = (char*) _mprAlloc(ctx, usize);
    if (newp) {
        memcpy(newp, ptr, usize);
    }
    return newp;
}


/*
 *  Allocate a block from a heap. Must be heap locked when called.
 */
MprBlk *_mprAllocBlock(MprCtx ctx, MprHeap *heap, MprBlk *parent, uint usize)
{
    MprBlk      *bp;
    Mpr         *mpr;
    uint        size;
#if BLD_CC_MMU
    MprRegion   *region;
#endif

    size = MPR_ALLOC_ALIGN(MPR_ALLOC_HDR_SIZE + usize);
    usize = size - MPR_ALLOC_HDR_SIZE;
    mpr = mprGetMpr(ctx);

    /*
     *  Check a memory allocation request against configured maximums and redlines. We do this so that 
     *  the application does not need to check the result of every little memory allocation. Rather, an 
     *  application-wide memory allocation failure can be invoked proactively when a memory redline is 
     *  exceeded. It is the application's responsibility to set the red-line value suitable for the system.
     */
    if (parent) {
        if ((size + mpr->alloc.bytesAllocated) > mpr->alloc.maxMemory) {
            /*
             *  Prevent allocation as over the maximum memory limit.
             */
            allocException(parent, size, 0);
            return 0;

        } else if ((size + mpr->alloc.bytesAllocated) > mpr->alloc.redLine) {
            /*
             *  Warn if allocation puts us over the red line. Then continue to grant the request.
             */
            allocException(parent, size, 1);
        }
    }

    lockHeap(heap);
#if BLD_CC_MMU
    if (likely(heap->flags & MPR_ALLOC_ARENA_HEAP)) {
        /*
         *  Allocate a block from an arena heap
         */
        region = heap->region;
        if ((region->nextMem + size) > &region->memory[region->size]) {
            if ((region = createRegion(ctx, heap, size)) == NULL) {
                unlockHeap(heap);
                return 0;
            }
        }
        bp = (MprBlk*) region->nextMem;
        bp->flags = 0;
        region->nextMem += size;

    } else if (likely(heap->flags & MPR_ALLOC_SLAB_HEAP)) {
        /*
         *  Allocate a block from a slab heap
         */
        region = heap->region;
        if ((bp = heap->freeList) != 0) {
            heap->freeList = bp->next;
            heap->reuseCount++;
        } else {
            if ((region->nextMem + size) > &region->memory[region->size]) {
                if ((region = createRegion(ctx, heap, size)) == NULL) {
                    unlockHeap(heap);
                    return 0;
                }
            }
            bp = (MprBlk*) region->nextMem;
            mprAssert(bp);
            region->nextMem += size;
        }
        bp->flags = 0;

    } else if (heap->flags & MPR_ALLOC_PAGE_HEAP) {
        if ((bp = (MprBlk*) mprMapAlloc(mpr, size, MPR_MAP_READ | MPR_MAP_WRITE)) == 0) {
            unlockHeap(heap);
            return 0;
        }
        bp->flags = 0;

    } else {
#endif
        if ((bp = (MprBlk*) allocMemory(size)) == 0) {
            unlockHeap(heap);
            return 0;
        }
        bp->flags = MPR_ALLOC_FROM_MALLOC;
#if BLD_CC_MMU
    }
#endif

    bp->children = 0;
    bp->parent = 0;
    bp->next = 0;
    bp->prev = 0;
    bp->size = size;
    SET_MAGIC(bp);

    if (parent) {
        linkBlock(parent, bp);
        incStats(heap, bp);

        if (heap != (MprHeap*) mpr) {
            mprSpinLock(&mpr->heap.spin);
            mpr->alloc.bytesAllocated += size;
            if (mpr->alloc.bytesAllocated > mpr->alloc.peakAllocated) {
                mpr->alloc.peakAllocated = mpr->alloc.bytesAllocated;
            }
            mprSpinUnlock(&mpr->heap.spin);
        } else {
            mpr->alloc.bytesAllocated += size;
            if (mpr->alloc.bytesAllocated > mpr->alloc.peakAllocated) {
                mpr->alloc.peakAllocated = mpr->alloc.bytesAllocated;
            }
        }
    }
    unlockHeap(heap);

#if BLD_FEATURE_MEMORY_DEBUG
    /*
     *  Catch uninitialized use
     */
    memset(GET_PTR(bp), 0xf7, usize);
    bp->seqno = allocCount++;
    if (bp == stopAlloc || bp->seqno == stopSeqno) {
        mprBreakpoint();
    }
#endif
#if BLD_FEATURE_MONITOR_STACK
    monitorStack(mpr);
#endif
    return bp;
}


/*
 *  Free a block back to a heap
 */
static void freeBlock(Mpr *mpr, MprHeap *heap, MprBlk *bp)
{
    int         size;
#if BLD_CC_MMU
    MprHeap     *hp;
    MprRegion   *region, *next;
#endif

    if (bp->flags & MPR_ALLOC_IS_HEAP && bp != GET_BLK(mpr)) {
#if BLD_CC_MMU
        hp = (MprHeap*) GET_PTR(bp);
        if (hp->depleted) {
            /*
             *  If there are depleted blocks, then the region contained in the heap memory block will be on 
             *  the depleted list. Must not free it here. Also, the region pointer for the original heap 
             *  block does not point to the start of the memory block to free.
             */
            region = hp->depleted;
            while (region) {
                next = region->next;
                if ((char*) region != ((char*) hp + sizeof(MprHeap))) {
                    /*
                     *  Don't free the initial region which is part of the heap (hp) structure
                     */
                    mprMapFree(region, region->vmSize);
                }
                region = next;
            }
            mprMapFree(hp->region, hp->region->vmSize);
        }
        mprMapFree(bp, bp->size);
#else
        freeMemory(bp);
#endif
        return;
    }
    size = bp->size;

    if (heap != (MprHeap*) mpr) {
        mprSpinLock(&mpr->heap.spin);
        mpr->alloc.bytesAllocated -= size;
        mprAssert(mpr->alloc.bytesAllocated >= 0);
        mprSpinUnlock(&mpr->heap.spin);
    } else {
        mpr->alloc.bytesAllocated -= size;
    }

#if BLD_CC_MMU
    if (!(bp->flags & MPR_ALLOC_FROM_MALLOC)) {
        if (heap->flags & MPR_ALLOC_ARENA_HEAP) {
            /*
             *  Just drop the memory. It will be reclaimed when the arena is freed.
             */
#if BLD_FEATURE_MEMORY_DEBUG
            bp->parent = 0;
            bp->next = 0;
            bp->prev = 0;
#endif
            return;

        } else if (heap->flags & MPR_ALLOC_SLAB_HEAP) {
            bp->next = heap->freeList;
            bp->prev = 0;
            bp->parent = 0;
            heap->freeList = bp;
            heap->freeListCount++;
            if (heap->freeListCount > heap->peakFreeListCount) {
                heap->peakFreeListCount = heap->freeListCount;
            }
            return;
        }
    }
#endif
    freeMemory(bp);
}


#if BLD_CC_MMU
/*
 *  Create a new region to satify the request if no memory exists in any depleted regions. 
 */
static MprRegion *createRegion(MprCtx ctx, MprHeap *heap, uint usize)
{
    MprRegion   *region;
    Mpr         *mpr;
    uint        size, regionSize, regionStructSize;

    mpr = mprGetMpr(ctx);

    /*
     *  Scavenge the depleted regions for scraps. We don't expect there to be many of these.
     */
    if (usize < 512) {
        for (region = heap->depleted; region; region = region->next) {
            if ((region->nextMem + usize) < &region->memory[region->size]) {
                return region;
            }
        }
    }

    /*
     *  Each time we grow the heap, double the size of the next region of memory. Use 30MB so we don't double regions
     *  that are just under 32MB.
     */
    if (heap->region->size <= (30 * 1024 * 1024)) {
        regionSize = heap->region->size * 2;
    } else {
        regionSize = heap->region->size;
    }

    regionStructSize = MPR_ALLOC_ALIGN(sizeof(MprRegion));
    size = max(usize, (regionStructSize + regionSize));
    size = MPR_PAGE_ALIGN(size, mpr->alloc.pageSize);
    usize = size - regionStructSize;

    if ((region = (MprRegion*) mprMapAlloc(mpr, size, MPR_MAP_READ | MPR_MAP_WRITE)) == 0) {
        return 0;
    }
    region->memory = (char*) region + regionStructSize;
    region->nextMem = region->memory;
    region->vmSize = size;
    region->size = usize;

    /*
     *  Move old region to depleted and install new region as the current heap region
     */
    heap->region->next = heap->depleted;
    heap->depleted = heap->region;
    heap->region = region;

    return region;
}
#endif


static void linkBlock(MprBlk *parent, MprBlk *bp)
{
#if BLD_FEATURE_MEMORY_VERIFY
    MprBlk      *sibling;

    /*
     *  Test that bp is not already in the list
     */
    mprAssert(bp != parent);
    for (sibling = parent->children; sibling; sibling = sibling->next) {
        mprAssert(sibling != bp);
    }
#endif

    /*
     *  Add to the front of the children
     */
    bp->parent = parent;
    if (parent->children) {
        parent->children->prev = bp;
    }
    bp->next = parent->children;
    parent->children = bp;
    bp->prev = 0;
}


static void unlinkBlock(MprBlk *bp)
{
    MprBlk      *parent;

    mprAssert(bp);

    parent = bp->parent;
    if (parent) {
        if (bp->prev) {
            bp->prev->next = bp->next;
        } else {
            parent->children = bp->next;
        }
        if (bp->next) {
            bp->next->prev = bp->prev;
        }
        bp->next = 0;
        bp->prev = 0;
        bp->parent = 0;
    }
}


#if BLD_FEATURE_MEMORY_STATS
static void incStats(MprHeap *heap, MprBlk *bp)
{
    if (unlikely(bp->flags & MPR_ALLOC_IS_HEAP)) {
        heap->reservedBytes += bp->size;
    } else {
        heap->totalAllocCalls++;
        heap->allocBlocks++;
        if (heap->allocBlocks > heap->peakAllocBlocks) {
            heap->peakAllocBlocks = heap->allocBlocks;
        }
        heap->allocBytes += bp->size;
        if (heap->allocBytes > heap->peakAllocBytes) {
            heap->peakAllocBytes = heap->allocBytes;
        }
    }
}


static void decStats(MprHeap *heap, MprBlk *bp)
{
    mprAssert(bp);

    if (unlikely(bp->flags & MPR_ALLOC_IS_HEAP)) {
        heap->reservedBytes += bp->size;
    } else {
        heap->allocBytes -= bp->size;
        heap->allocBlocks--;
    }
    mprAssert(heap->allocBytes >= 0);
}
#endif


#if BLD_FEATURE_MONITOR_STACK
static void monitorStack(Mpr *mpr)
{
    /*
     *  Monitor stack usage
     */
    int diff = (int) ((char*) mpr->alloc.stackStart - (char*) &diff);
    if (diff < 0) {
        mpr->alloc.peakStack -= diff;
        mpr->alloc.stackStart = (void*) &diff;
        diff = 0;
    }
    if (diff > mpr->alloc.peakStack) {
        mpr->alloc.peakStack = diff;
    }
}
#endif


static void initHeap(MprHeap *heap, cchar *name, bool threadSafe)
{
    heap->name = name;
    heap->region = 0;
    heap->depleted = 0;
    heap->flags = 0;
    heap->objSize = 0;
    heap->freeList = 0;
    heap->freeListCount = 0;
    heap->reuseCount = 0;

#if BLD_FEATURE_MEMORY_STATS
    heap->allocBlocks = 0;
    heap->peakAllocBlocks = 0;
    heap->allocBytes = 0;
    heap->peakAllocBytes = 0;
    heap->totalAllocCalls = 0;
    heap->peakFreeListCount = 0;
#endif

    heap->notifier = 0;
    heap->notifierCtx = 0;

#if BLD_FEATURE_MULTITHREAD
    if (threadSafe) {
        mprInitSpinLock(heap, &heap->spin);
        heap->flags |= MPR_ALLOC_THREAD_SAFE;
    }
#endif
}


/*
 *  Find the heap from which a block has been allocated. Chase up the parent chain.
 */
MprHeap *mprGetHeap(MprBlk *bp)
{
    mprAssert(bp);
    mprAssert(VALID_BLK(bp));

    while (!(bp->flags & MPR_ALLOC_IS_HEAP)) {
        bp = bp->parent;
        mprAssert(bp);
    }
    return (MprHeap*) GET_PTR(bp);
}


void mprSetAllocNotifier(MprCtx ctx, MprAllocNotifier cback)
{
    MprHeap     *heap;

    heap = mprGetHeap(GET_BLK(ctx));
    heap->notifier = cback;
    heap->notifierCtx = ctx;
}


/*
 *  Monitor stack usage. Return true if the stack has grown. Uses no locking and thus yields approximate results.
 */
bool mprStackCheck(MprCtx ptr)
{
    Mpr     *mpr;
    int     size;

    mprAssert(VALID_CTX(ptr));

    mpr = mprGetMpr(ptr);
    size = (int) ((char*) mpr->alloc.stackStart - (char*) &size);
    if (size < 0) {
        mpr->alloc.peakStack -= size;
        mpr->alloc.stackStart = (void*) &size;
        size = 0;
    }
    if (size > mpr->alloc.peakStack) {
        mpr->alloc.peakStack = size;
        return 1;
    }
    return 0;
}


void mprSetAllocLimits(MprCtx ctx, int redLine, int maxMemory)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    if (redLine > 0) {
        mpr->alloc.redLine = redLine;
    }
    if (maxMemory > 0) {
        mpr->alloc.maxMemory = maxMemory;
    }
}


void *mprGetParent(MprCtx ptr)
{
    MprBlk  *bp;

    if (ptr == 0) {
        return 0;
    }

    mprAssert(VALID_CTX(ptr));

    bp = GET_BLK(ptr);
    mprAssert(VALID_BLK(bp));
    mprAssert(bp->parent);

    return GET_PTR(bp->parent);
}


MprAlloc *mprGetAllocStats(MprCtx ctx)
{
    MprAlloc    *ap;

#if LINUX
    struct rusage   rusage;
    char            buf[1024], *cp;
    int             fd, len;

    getrusage(RUSAGE_SELF, &rusage);
    ap = &mprGetMpr(ctx)->alloc;
    ap->rss = rusage.ru_maxrss;

    ap->ram = MAXINT64;
    if ((fd = open("/proc/meminfo", O_RDONLY)) >= 0) {
        if ((len = read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[len] = '\0';
            if ((cp = strstr(buf, "MemTotal:")) != 0) {
                for (; *cp && !isdigit((int) *cp); cp++) {}
                ap->ram = ((int64) atoi(cp) * 1024);
            }
        }
        close(fd);
    }
#elif MACOSX || FREEBSD
    struct rusage   rusage;
    int64           ram, usermem;
    size_t          len;
    int             mib[2];

    getrusage(RUSAGE_SELF, &rusage);
    ap = &mprGetMpr(ctx)->alloc;
    ap->rss = rusage.ru_maxrss;

    mib[0] = CTL_HW;
#if MACOSX
    mib[1] = HW_MEMSIZE;
#else
    mib[1] = HW_PHYSMEM;
#endif
    len = sizeof(ram);
    sysctl(mib, 2, &ram, &len, NULL, 0);
    ap->ram = ram;

    mib[0] = CTL_HW;
    mib[1] = HW_USERMEM;
    len = sizeof(usermem);
    sysctl(mib, 2, &usermem, &len, NULL, 0);
    ap->user = usermem;
#else
    ap = &mprGetMpr(ctx)->alloc;
#endif
    return ap;
}


int64 mprGetUsedMemory(MprCtx ctx)
{
    return mprGetMpr(ctx)->alloc.bytesAllocated;
}


int mprIsValid(MprCtx ptr)
{
    MprBlk  *bp;

    bp = GET_BLK(ptr);
    return (bp && VALID_BLK(bp));
}


#if !BLD_HAS_GLOBAL_MPR || BLD_WIN_LIKE
/*
 *  Get the ultimate block parent
 */
Mpr *mprGetMpr(MprCtx ctx)
{
#if BLD_WIN_LIKE
    /*  Windows can use globalMpr but must have a function to solve linkage issues */
    return (Mpr*) _globalMpr;
#else
    MprBlk  *bp = GET_BLK(ctx);

    while (bp && bp->parent) {
        bp = bp->parent;
    }
    return (Mpr*) GET_PTR(bp);
#endif
}
#endif



bool mprHasAllocError(MprCtx ctx)
{
    MprBlk  *bp;

    bp = GET_BLK(ctx);
    return (bp->flags & MPR_ALLOC_HAS_ERROR) ? 1 : 0;
}


/*
 *  Reset the allocation error flag at this block and all parent blocks
 */
void mprResetAllocError(MprCtx ctx)
{
    MprBlk  *bp;

    bp = GET_BLK(ctx);
    while (bp) {
        bp->flags &= ~MPR_ALLOC_HAS_ERROR;
        bp = bp->parent;
    }
}



/*
 *  Set the allocation error flag at this block and all parent blocks
 */
void mprSetAllocError(MprCtx ctx)
{
    MprBlk  *bp;

    bp = GET_BLK(ctx);
    while (bp) {
        bp->flags |= MPR_ALLOC_HAS_ERROR;
        bp = bp->parent;
    }
}


/*
 *  Called to invoke the memory failure handler on a memory allocation error
 */
static void allocException(MprBlk *parent, uint size, bool granted)
{
    MprHeap     *hp;
    Mpr         *mpr;

    mprAssert(VALID_BLK(parent));

    mpr = mprGetMpr(parent);
    mprSpinLock(&mpr->heap.spin);
    if (mpr->alloc.inAllocException == 0) {
        mpr->alloc.inAllocException = 1;
        mprSpinUnlock(&mpr->heap.spin);

        /*
         *  Notify all the heaps up the chain
         */
        for (hp = mprGetHeap(parent); hp; hp = mprGetHeap(parent)) {
            if (hp->notifier) {
                (hp->notifier)(hp->notifierCtx, size, (int) mpr->alloc.bytesAllocated, granted);
                break;
            }
            parent = parent->parent;
            if (parent == 0) {
                break;
            }
        }
        mpr->alloc.inAllocException = 0;
    } else {
        mprSpinUnlock(&mpr->heap.spin);
    }
}


/*
 *  Handle an allocation error
 */
static void allocError(MprBlk *parent, uint size)
{
    Mpr     *mpr;

    mpr = mprGetMpr(parent);
    mpr->alloc.errors++;
    mprSetAllocError(GET_PTR(parent));
    allocException(parent, size, 0);
}


/*
 *  Get information about the system. Get page size and number of CPUs.
 */
static void sysinit(Mpr *mpr)
{
    MprAlloc    *ap;

    ap = &mpr->alloc;

    ap->numCpu = 1;

#if MACOSX
    #ifdef _SC_NPROCESSORS_ONLN
        ap->numCpu = (int) sysconf(_SC_NPROCESSORS_ONLN);
    #else
        ap->numCpu = 1;
    #endif
    ap->pageSize = (int) sysconf(_SC_PAGESIZE);
#elif SOLARIS
{
    FILE *ptr;
    if  ((ptr = popen("psrinfo -p", "r")) != NULL) {
        fscanf(ptr, "%d", &alloc.numCpu);
        (void) pclose(ptr);
    }
    alloc.pageSize = sysconf(_SC_PAGESIZE);
}
#elif BLD_WIN_LIKE
{
    SYSTEM_INFO     info;

    GetSystemInfo(&info);
    ap->numCpu = info.dwNumberOfProcessors;
    ap->pageSize = info.dwPageSize;

}
#elif FREEBSD
    {
        int     cmd[2];
        size_t  len;

        /*
         *  Get number of CPUs
         */
        cmd[0] = CTL_HW;
        cmd[1] = HW_NCPU;
        len = sizeof(ap->numCpu);
        if (sysctl(cmd, 2, &ap->numCpu, &len, 0, 0) < 0) {
            ap->numCpu = 1;
        }

        /*
         *  Get page size
         */
        ap->pageSize = sysconf(_SC_PAGESIZE);
    }
#elif LINUX
    {
        static const char processor[] = "processor\t:";
        char    c;
        int     fd, col, match;

        fd = open("/proc/cpuinfo", O_RDONLY);
        if (fd < 0) {
            return;
        }
        match = 1;
        for (col = 0; read(fd, &c, 1) == 1; ) {
            if (c == '\n') {
                col = 0;
                match = 1;
            } else {
                if (match && col < (sizeof(processor) - 1)) {
                    if (c != processor[col]) {
                        match = 0;
                    }
                    col++;

                } else if (match) {
                    ap->numCpu++;
                    match = 0;
                }
            }
        }
        --ap->numCpu;
        close(fd);

        /*
         *  Get page size
         */
        ap->pageSize = sysconf(_SC_PAGESIZE);
    }
#else
        ap->pageSize = 4096;
#endif
    if (ap->pageSize <= 0 || ap->pageSize >= (16 * 1024)) {
        ap->pageSize = 4096;
    }
}


int mprGetPageSize(MprCtx ctx)
{
    Mpr     *mpr;

    mpr = mprGetMpr(ctx);
    return mpr->alloc.pageSize;
}


/*
 *  Virtual memory support. Map virutal memory into the address space and commit.
 */
void *mprMapAlloc(MprCtx ctx, uint size, int mode)
{
    Mpr     *mpr;
    void    *ptr;

    mpr = mprGetMpr(ctx);
    size = MPR_PAGE_ALIGN(size, mpr->alloc.pageSize);

#if BLD_CC_MMU
    /*
     *  Has virtual memory
     */
    #if BLD_UNIX_LIKE
        ptr = mmap(0, size, mode, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (ptr == (void*) -1) {
            ptr = 0;
        }
    #elif BLD_WIN_LIKE
        ptr = VirtualAlloc(0, size, MEM_RESERVE | MEM_COMMIT, mapProt(mode));
    #else
        ptr = malloc(size);
    #endif
#else
    /*
     *  No MMU
     */
    ptr = malloc(size);
#endif

    if (ptr == 0) {
        return 0;
    }
    return ptr;
}


void mprMapFree(void *ptr, uint size)
{
#if BLD_CC_MMU
    /*
     *  Has virtual memory
     */
    #if BLD_UNIX_LIKE
        if (munmap(ptr, size) != 0) {
            mprAssert(0);
        }
    #elif BLD_WIN_LIKE
        VirtualFree(ptr, 0, MEM_RELEASE);
    #else
        free(ptr);
    #endif
#else
    /*
     *  Has no MMU
     */
    free(ptr);
#endif
}


#if BLD_WIN_LIKE
static int mapProt(int flags)
{
    if (flags & MPR_MAP_EXECUTE) {
        return PAGE_EXECUTE_READWRITE;
    } else if (flags & MPR_MAP_WRITE) {
        return PAGE_READWRITE;
    }
    return PAGE_READONLY;
}
#endif


/*
 *  Actually allocate memory. Just use ordinary malloc. Arenas and slabs will use MapAlloc instead.
 */
static void *allocMemory(uint size)
{
    return malloc(size);
}


static void freeMemory(MprBlk *bp)
{
#if BLD_FEATURE_MEMORY_DEBUG
    int     size;
    
    /*
     *  Free with unique signature to catch block-reuse
     */
    size = bp->size;
    memset(bp, 0xF1, size);
#endif
    free(bp);
}


void mprValidateBlock(MprCtx ctx)
{
#if BLD_FEATURE_MEMORY_DEBUG
    Mpr         *mpr;
    MprBlk      *bp, *parent, *sibling, *child;

    mprAssert(VALID_CTX(ctx));

    bp = GET_BLK(ctx);
    mpr = mprGetMpr(ctx);

    if (bp == GET_BLK(mpr)) {
        return;
    }

    mprAssert(bp->parent);
    mprAssert(VALID_BLK(bp->parent));
    parent = bp->parent;

    /*
     *  Find this block in the parent chain
     */
    for (sibling = parent->children; sibling; sibling = sibling->next) {
        mprAssert(VALID_BLK(sibling));
        mprAssert(sibling != parent);
        mprAssert(sibling->parent == parent);
        if (sibling->children) {
            mprAssert(VALID_BLK(sibling->children));
        }
        if (sibling == bp) {
            break;
        }
    }
    mprAssert(sibling);

    /*
     *  Check the rest of the siblings
     */
    if (sibling) {
        for (sibling = sibling->next; sibling; sibling = sibling->next) {
            mprAssert(VALID_BLK(sibling));
            mprAssert(sibling != parent);
            mprAssert(sibling->parent == parent);
            if (sibling->children) {
                mprAssert(VALID_BLK(sibling->children));
            }
            mprAssert(sibling != bp);
        }
    }

    /*
     *  Validate children (recursively)
     */
    for (child = bp->children; child; child = child->next) {
        mprAssert(child != bp);
        mprValidateBlock(GET_PTR(child));
    }
#endif
}


#if BLD_FEATURE_MEMORY_STATS

#define percent(a,b) ((a / 1000) * 100 / (b / 1000))

/*
 *  Traverse all blocks and look for heaps
 */
static void printMprHeaps(MprCtx ctx)
{
    MprAlloc    *ap;
    MprBlk      *bp, *child;
    MprHeap     *heap;
    MprRegion   *region;
    cchar       *kind;
    int         available, total, remaining;

    bp = MPR_GET_BLK(ctx);

    if (bp->size & MPR_ALLOC_IS_HEAP) {
        ap = mprGetAllocStats(ctx);
        heap = (MprHeap*) ctx;
        if (heap->flags & MPR_ALLOC_PAGE_HEAP) {
            kind = "page";
        } else if (heap->flags & MPR_ALLOC_ARENA_HEAP) {
            kind = "arena";
        } else if (heap->flags & MPR_ALLOC_SLAB_HEAP) {
            kind = "slab";
        } else {
            kind = "general";
        }
        mprLog(ctx, 0, "\n    Heap                     %10s (%s)",       heap->name, kind);

        available = 0;
        total = 0;
        for (region = heap->depleted; region; region = region->next) {
            available += (int) (region->size - (region->nextMem - region->memory));
            total += region->size;
        }
        remaining = 0;
        if (heap->region) {
            total += heap->region->size;
            remaining = (int) (region->size - (region->nextMem - region->memory));
        }

        mprLog(ctx, 0, "    Allocated memory         %,10d K",          heap->allocBytes / 1024);
        mprLog(ctx, 0, "    Peak heap memory         %,10d K",          heap->peakAllocBytes / 1024);
        mprLog(ctx, 0, "    Allocated blocks         %,10d",            heap->allocBlocks);
        mprLog(ctx, 0, "    Peak heap blocks         %,10d",            heap->peakAllocBlocks);
        mprLog(ctx, 0, "    Alloc calls              %,10d",            heap->totalAllocCalls);

        if (heap->flags & (MPR_ALLOC_PAGE_HEAP | MPR_ALLOC_ARENA_HEAP | MPR_ALLOC_SLAB_HEAP)) {
            mprLog(ctx, 0, "    Heap Regions             %,10d K",      total / 1024);
            mprLog(ctx, 0, "    Depleted regions         %,10d K",      available / 1024);
            if (heap->region) {
                mprLog(ctx, 0, "    Unallocated memory       %,10d K",  remaining / 1024);
            }            
        }
            
        if (heap->flags & MPR_ALLOC_PAGE_HEAP) {
            mprLog(ctx, 0, "    Page size                %,10d",         ap->pageSize);

        } else if (heap->flags & MPR_ALLOC_ARENA_HEAP) {

        } else if (heap->flags & MPR_ALLOC_SLAB_HEAP) {
            mprLog(ctx, 0, "    Heap object size         %,10d bytes",   heap->objSize);
            mprLog(ctx, 0, "    Heap free list count     %,10d",         heap->freeListCount);
            mprLog(ctx, 0, "    Heap peak free list      %,10d",         heap->peakFreeListCount);
            mprLog(ctx, 0, "    Heap reuse count         %,10d",         heap->reuseCount);
        }
    }
    for (child = bp->children; child; child = child->next) {
        printMprHeaps(MPR_GET_PTR(child));
    }
}
#endif


void mprPrintAllocReport(MprCtx ctx, cchar *msg)
{
#if BLD_FEATURE_MEMORY_STATS
    MprAlloc    *ap;

    ap = &mprGetMpr(ctx)->alloc;

    /*
     *  MPR stats
     */
    mprLog(ctx, 0, "\n\n\nMPR Memory Report %s", msg);
    mprLog(ctx, 0, "------------------------------------------------------------------------------------------\n");
    mprLog(ctx, 0, "  Current heap memory  %,14d K",              ap->bytesAllocated / 1024);
    mprLog(ctx, 0, "  Peak heap memory     %,14d K",              ap->peakAllocated / 1024);
    mprLog(ctx, 0, "  Peak stack size      %,14d K",              ap->peakStack / 1024);
    mprLog(ctx, 0, "  Allocation errors    %,14d",                ap->errors);
    
    /*
     *  Limits
     */
    mprLog(ctx, 0, "  Memory limit         %,14d MB (%d %%)",    ap->maxMemory / (1024 * 1024), 
           percent(ap->bytesAllocated, ap->maxMemory));
    mprLog(ctx, 0, "  Memory redline       %,14d MB (%d %%)",    ap->redLine / (1024 * 1024), 
           percent(ap->bytesAllocated, ap->redLine));

    /*
     *  Heaps
     */
    mprLog(ctx, 0, "\n  Heaps");
    mprLog(ctx, 0, "  -----");
    printMprHeaps(ctx);
#endif /* BLD_FEATURE_MEMORY_STATS */
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

