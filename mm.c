/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "team05",
    /* First member's full name */
    "SON",
    /* First member's email address */
    "hieronimus92@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

// sg or db word allignment
#define ALIGNMENT 8
// round-up ALIGNMENTx
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// sg, db word and chunk size allignment
#define WSIZE (4)
#define DSIZE (8)
#define CHUNKSIZE (1<<12)
// return max(x, y)
#define MAX(x, y) ((x) > (y) ? (x) : (y))
// pack size and alloc
#define PACK(size, alloc) ((size) | (alloc))
// read or write at addr(p)
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
// get size or alloc from addr(p)
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
// get hdr, ftr addr from block ptr(bp)
#define HDRP(bp) ((char *)(bp) - WSIZE) // hdr = bp - word
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // ftr = bp + size - db word
// get prev, next block addr from block ptr(bp)
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE)) // prev = curr - prev_size
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE)) // next = curr + curr_size

// set heap_list
static void *heap_listp = NULL;
static void *last_bp = NULL; // set last_bp(next fit)

// coalesce alloc or free blocks
static void *mm_coalesce(void *bp)
{
    size_t prev_alloc = 0;
    size_t next_alloc = 0;
    size_t size = 0;

    prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size = GET_SIZE(HDRP(bp));
    if (prev_alloc && next_alloc) // case 1: prev, next alloc
        return (bp);
    else if (prev_alloc && !next_alloc) // case 2: prev alloc, next free >> curr + next
    {
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // curr_size + next_size
        PUT(HDRP(bp), PACK(size, 0)); // reset curr_hdr
        PUT(FTRP(bp), PACK(size, 0)); // reset next_ftr
        // memset(bp, 0, size-DSIZE); // init free block
    }
    else if (!prev_alloc && next_alloc) // case 3: prev free, next alloc >> prev + curr
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))); // prev_size + curr_size
        PUT(FTRP(bp), PACK(size, 0)); // reset curr_ftr
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // reset prev_hdr
        bp = PREV_BLKP(bp); // bp >> prev
        // memset(bp, 0, size-DSIZE); // init free block
    }
    else // case 4: prev, next free >> prev + curr + next
    {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); // prev_size + curr_size + next_size
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // reset prev_hdr
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // reset next_ftr
        bp = PREV_BLKP(bp); // bp >> prev
        // memset(bp, 0, size-DSIZE); // init free block
    }
    last_bp = bp; // last fit
    return (bp);
}

// extend additional heap
static void *extend_heap(size_t words)
{
    char *bp = NULL;
    size_t size = 0;

    size = (words%2) ? (words+1) * WSIZE : words * WSIZE;
    if ((bp = mem_sbrk(size)) == (void *)-1)
        return (NULL); // err: mem_sbrk failed(out of memory)
    PUT(HDRP(bp), PACK(size, 0)); // new block hdr
    PUT(FTRP(bp), PACK(size, 0)); // new block ftr
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // new block | eplg hdr
    return (mm_coalesce(bp)); // set all new block free
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return (-1);
    PUT(heap_listp, 0); // alignment padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // prlg hdr
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // prlg ftr
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); // eplg hdr
    heap_listp += (2*WSIZE); // heap_listp >> prlg ftr
    if (extend_heap(CHUNKSIZE / WSIZE) == NULL) // set init heap size
        return (-1); // err: extend heap failed
    last_bp = heap_listp; // next fit
    return (0);
}

// first fit
static void *mm_first_fit(size_t alloc_size)
{
    void *bp = NULL;

    bp = heap_listp;
    while (GET_SIZE(HDRP(bp)))
    {
        if (!GET_ALLOC(HDRP(bp)) && (GET_SIZE(HDRP(bp)) >= alloc_size))
            return (bp);
        bp = NEXT_BLKP(bp);
    }
    return (NULL); // err: no fit
}

// next fit
static void *mm_next_fit(size_t alloc_size)
{
    void *bp = NULL;

    bp = last_bp;
    while (GET_SIZE(HDRP(bp)))
    {
        if (!GET_ALLOC(HDRP(bp)) && (GET_SIZE(HDRP(bp)) >= alloc_size))
            return (bp);
        bp = NEXT_BLKP(bp);
    }
    return (NULL); //  err: no fit
}

static void mm_place(void *bp, size_t alloc_size)
{
    size_t curr_size = 0;

    curr_size = GET_SIZE(HDRP(bp));
    if (curr_size - alloc_size >= 2 * DSIZE) // rest_size >= hdr + ftr
    {
        PUT(HDRP(bp), PACK(alloc_size, 1)); // set new alloc block hdr
        PUT(FTRP(bp), PACK(alloc_size, 1)); // set new alloc block ftr
        bp = NEXT_BLKP(bp); // bp >> next
        PUT(HDRP(bp), PACK(curr_size - alloc_size, 0)); // set rest block hdr
        PUT(FTRP(bp), PACK(curr_size - alloc_size, 0)); // set rest block ftr
    }
    else
    {
        PUT(HDRP(bp), PACK(curr_size, 1)); // not exist rest
        PUT(FTRP(bp), PACK(curr_size, 1)); // not exist rest
    }
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t alloc_size = 0;
    size_t extend_size;
    char *bp;
    
    if (size == 0)
        return (NULL); // err: alloc unavailable
    if (size <= DSIZE)
        alloc_size = 2 * DSIZE; // set hdr, ftr
    else
        alloc_size = DSIZE * ((size + DSIZE + DSIZE-1) / DSIZE); // round-up 8x (size+hdr+ftr)
    // if ((bp = mm_first_fit(alloc_size)) != NULL) // first fit found
    if ((bp = mm_next_fit(alloc_size)) != NULL) // next fit found
    {
        mm_place(bp, alloc_size); // alloc at fit
        last_bp = bp; // next fit
        return (bp);
    }
    extend_size = MAX(alloc_size, CHUNKSIZE); // no fit >> extend heap
    if ((bp = extend_heap(extend_size / WSIZE)) == NULL)
        return (NULL); // err: extend_heap failed(mem_sbrk failed(out of memory))
    mm_place(bp, alloc_size); // alloc at extended
    last_bp = bp; // next fit
    return (bp);
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = 0;

    size = GET_SIZE(HDRP(ptr)); // free block size
    PUT(HDRP(ptr), PACK(size, 0)); // hdr: alloc >> free
    PUT(FTRP(ptr), PACK(size, 0)); // ftr: alloc >> free
    mm_coalesce(ptr);
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *old = NULL;
    void *new = NULL;
    size_t cpy_size = 0;

    old = ptr;
    new = mm_malloc(size);
    if (new == NULL)
        return (NULL); // err: malloc failed
    cpy_size = GET_SIZE(HDRP(old));
    if (size < cpy_size)
        cpy_size = size;
    memcpy(new, old, cpy_size);
    mm_free(old);
    return (new);
}
