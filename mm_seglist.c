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

// align size
#define ALIGNMENT (8)
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)
#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
// sg, db word and chunk size alignment
#define WSIZE (4)
#define DSIZE (8)
#define INIT_CHUNKSIZE (1<<6)
#define CHUNKSIZE (1<<12)
// return max(x, y)
#define MAX(x, y) ((x) > (y) ? (x) : (y))
// pack size and alloc
#define PACK(size, alloc) ((size) | (alloc))
// read or write at addr(p)
#define GET(p) (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))
#define PUT_PTR(p, ptr) (*(unsigned int *)(p) = (unsigned int)(ptr))
// get size or alloc from addr(p)
#define GET_SIZE(p) (GET(p) & ~0x7)
#define GET_ALLOC(p) (GET(p) & 0x1)
// get hdr, ftr addr from block ptr(bp)
#define HDRP(bp) ((char *)(bp) - WSIZE) // hdr = bp - word
#define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) // ftr = bp + size - db word
// get prev, next block addr from block ptr(bp)
#define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE)) // prev = curr - prev_size
#define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE)) // next = curr + curr_size
// get prev, next addr in free_list(seg_list[i])
#define PREV_PTR(ptr) ((char *)(ptr) + WSIZE)
#define NEXT_PTR(ptr) ((char *)(ptr))
// get prev, next addr in seg_list
#define PREV(ptr) (*(char **)(PREV_PTR(ptr)))
#define NEXT(ptr) (*(char **)(ptr))
// init seg_list
#define MAX_LIST (20)

// set heap_list
static void *heap_listp = NULL;
// set seg_list
static void *seg_list[MAX_LIST];

// insert, delete in list
static void mm_list_insert(void *ptr, size_t size)
{
    int i = 0;
    void *sp = NULL; // search ptr
    void *ip = NULL; // insert ptr

    while ((i < MAX_LIST-1) && (size > 1))
    {
        size >>= 1; // find size
        i++;
    }
    sp = seg_list[i]; // find at free_list in seg_list
    while (sp && (size > GET_SIZE(HDRP(sp))))
    {
        ip = sp; // ip >> prev
        sp = NEXT(sp); // sp >> next
    }
    if (ip && sp) // case 1: prev, next alloc
    {
        PUT_PTR(NEXT_PTR(ptr), sp); // ptr.next = next
        PUT_PTR(PREV_PTR(sp), ptr); // next.prev = ptr
        PUT_PTR(PREV_PTR(ptr), ip); // ptr.prev = prev
        PUT_PTR(NEXT_PTR(ip), ptr); // prev.next = ptr
    }
    else if (ip && !sp) // case 2: prev alloc, next free
    {
        PUT_PTR(NEXT_PTR(ptr), NULL); // ptr.next = NULL
        PUT_PTR(PREV_PTR(ptr), ip); // ptr.prev = prev
        PUT_PTR(NEXT_PTR(ip), ptr); // prev.next = ptr
    }
    else if (!ip && sp) // case 3: prev free, next alloc
    {
        PUT_PTR(NEXT_PTR(ptr), sp); // ptr.next = next
        PUT_PTR(PREV_PTR(sp), ptr); // next.prev = ptr
        PUT_PTR(PREV_PTR(ptr), NULL); // ptr.prev = NULL
        seg_list[i] = ptr; // seg_list[i].front = ptr
    }
    else // case 4: prev, next free
    {
        PUT_PTR(NEXT_PTR(ptr), NULL); // ptr.next = NULL
        PUT_PTR(PREV_PTR(ptr), NULL); // ptr.prev = NULL
        seg_list[i] = ptr; // seg_list[i].front = ptr
    }
}

static void mm_list_delete(void *ptr)
{
    int i = 0;
    size_t size = 0;

    size = GET_SIZE(HDRP(ptr));
    while ((i < MAX_LIST-1) && (size > 1))
    {
        size >>= 1; // find size
        i++;
    }
    if (PREV(ptr) && NEXT(ptr)) // case 1: prev, next alloc
    {
        PUT_PTR(PREV_PTR(NEXT(ptr)), PREV(ptr)); // next.prev = prev
        PUT_PTR(NEXT_PTR(PREV(ptr)), NEXT(ptr)); // prev.next = next
    }
    else if (PREV(ptr) && !NEXT(ptr)) // case 2: prev alloc, next free
        PUT_PTR(NEXT_PTR(PREV(ptr)), NULL); // next.prev = NULL
    else if (!PREV(ptr) && NEXT(ptr)) // case 3: prev free, next alloc
    {
        PUT_PTR(PREV_PTR(NEXT(ptr)), NULL); // next.prev = NULL
        seg_list[i] = NEXT(ptr); // seg_list[i].front = next
    }
    else // case 4: prev, next free
        seg_list[i] = NULL; // seg_list[i].front = NULL
}

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
    {
        return (bp);
    }
    if (prev_alloc && !next_alloc) // case 2: prev alloc, next free >> curr + next
    {
        mm_list_delete(bp);
        mm_list_delete(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // curr_size + next_size
        PUT(HDRP(bp), PACK(size, 0)); // reset curr_hdr
        PUT(FTRP(bp), PACK(size, 0)); // reset next_ftr
    }
    else if (!prev_alloc && next_alloc) // case 3: prev free, next alloc >> prev + curr
    {
        mm_list_delete(bp);
        mm_list_delete(PREV_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))); // prev_size + curr_size
        PUT(FTRP(bp), PACK(size, 0)); // reset curr_ftr
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // reset prev_hdr
        bp = PREV_BLKP(bp); // bp >> prev
    }
    else // case 4: prev, next free >> prev + curr + next
    {
        mm_list_delete(bp);
        mm_list_delete(PREV_BLKP(bp));
        mm_list_delete(NEXT_BLKP(bp));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); // prev_size + curr_size + next_size
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // reset prev_hdr
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // reset next_ftr
        bp = PREV_BLKP(bp); // bp >> prev
    }
    mm_list_insert(bp, size);
    return (bp);
}

// extend additional heap
static void *mm_extend_heap(size_t size)
{
    char *bp = NULL;

    if ((bp = mem_sbrk(size)) == (void *)-1)
        return (NULL); // err: mem_sbrk failed(ran out of memory)
    PUT(HDRP(bp), PACK(size, 0)); // new block hdr
    PUT(FTRP(bp), PACK(size, 0)); // new block ftr
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // new block | eplg hdr
    mm_list_insert(bp, size);
    return (mm_coalesce(bp)); // set all new block free
}

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{
    int i = 0;
    while (i < MAX_LIST)
        seg_list[i++] = NULL;
    if ((heap_listp = mem_sbrk(4 * WSIZE)) == (void *)-1)
        return (-1); // err: mem_sbrk failed(ran out of memory)
    PUT(heap_listp, 0); // alignment padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // prlg hdr
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // prlg ftr
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); // eplg hdr
    heap_listp += (2*WSIZE); // heap_listp >> prlg ftr
    if (mm_extend_heap(INIT_CHUNKSIZE) == NULL) // set init heap size
        return (-1); // err: extend heap failed
    return (0);
}

// first fit
static void *mm_find_fit(size_t alloc_size)
{
    void *bp = NULL;
    int i = 0;
    size_t search_size = 0;

    search_size = alloc_size;
    while (i < MAX_LIST)
    {
        if ((i == MAX_LIST-1) || ((search_size <= 1) && seg_list[i]))
        {
            bp = seg_list[i];
            while (bp && (alloc_size > GET_SIZE(HDRP(bp))))
                bp = NEXT(bp);
            if (bp)
                return bp;
        }
        search_size >>= 1;
        i++;
    }
    return (NULL); // err: no fit
}

static void mm_place(void *bp, size_t alloc_size)
{
    size_t curr_size = 0;

    curr_size = GET_SIZE(HDRP(bp));
    mm_list_delete(bp);
    if (curr_size - alloc_size > 2 * DSIZE) // rest_size >= hdr + ftr
    {
        PUT(HDRP(bp), PACK(alloc_size, 1)); // set new alloc block hdr
        PUT(FTRP(bp), PACK(alloc_size, 1)); // set new alloc block ftr
        bp = NEXT_BLKP(bp); // bp >> next
        PUT(HDRP(bp), PACK(curr_size - alloc_size, 0)); // set rest block hdr
        PUT(FTRP(bp), PACK(curr_size - alloc_size, 0)); // set rest block ftr
        mm_list_insert(bp, (curr_size - alloc_size));
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
    size_t extend_size = 0;
    char *bp = NULL;
    
    if (size == 0)
        return (NULL); // err: alloc unavailable
    alloc_size = ALIGN(size) + DSIZE; // alloc_size = aligned size + hdr + ftr
    if ((bp = mm_find_fit(alloc_size)) != NULL) // fit found
    {
        mm_place(bp, alloc_size); // alloc at fit
        return (bp);
    }
    extend_size = MAX(alloc_size, CHUNKSIZE); // no fit >> extend heap
    if ((bp = mm_extend_heap(extend_size)) == NULL)
        return (NULL); // err: mm_extend_heap failed(mem_sbrk failed(out of memory))
    mm_place(bp, alloc_size); // alloc at extended
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
    mm_list_insert(ptr, size);
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

    size = ALIGN(size); // align size
    old = ptr;
    new = mm_malloc(size);
    if (new == NULL)
        return (NULL); // err: malloc failed
    cpy_size = GET_SIZE(HDRP(old)) - DSIZE;
    if (size < cpy_size)
        cpy_size = size;
    memcpy(new, old, cpy_size);
    mm_free(old);
    return (new);
}
