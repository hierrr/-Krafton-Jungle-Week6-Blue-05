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
    "team 8",
    /* First member's full name */
    "Heo Wonyoung",
    /* First member's email address */
    "dnjsdud2257@gmail.com",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* 기본 상수 및 macros*/

#define WSIZE 4
#define DSIZE 8 
#define CHUNKSIZE (1<<12)

#define MAX(x,y) ((x) > (y) ? (x): (y))

// 헤더와 푸터에 저장할 수 있는 값 리턴
#define PACK(size, alloc) ((size) | (alloc))

/* 크기와 할당 비트를 통합해서 헤더와 푸터에 저장할 수 있는 값을 리턴*/
#define GET(p)      (*(unsigned int *)(p))
#define PUT(p, val) (*(unsigned int *)(p) = (val))

/* 주소 p의 헤더 또는 푸터의 SIZE와 할당 비트를 리턴한다.*/
#define GET_SIZE(p)   (GET(p) & ~0x7) // 뒤에 3비트를 제외하고 읽어옴
#define GET_ALLOC(p)  (GET(p) & 0x1) // 할당 가용 확인

/* 각각 블록 헤더와 풋터를 가리키는 포인터를 리턴한다.*/
#define HDRP(bp)    ((char *)(bp) - WSIZE)  //bp는 헤더다음 즉 데이터블럭의 시작지점을 말하는듯
#define FTRP(bp)    ((char *)(bp) + GET_SIZE(HDRP(bp))- DSIZE)

/* 다음과 이전 블록 포인터를 각각 리턴한다.*/
#define NEXT_BLKP(bp)   (((char *)(bp) + GET_SIZE((char *)(bp) - WSIZE))) 
#define PREV_BLKP(bp)   (((char *)(bp) - GET_SIZE((char *)(bp) - DSIZE)))

// 전역 힙 변수 및 함수 선언
static void *heap_listp;
static void *last_modip;  // next-fit방식을 위함
static void *extend_heap(size_t words);
static void *coalesce(void *bp);
static void *find_fit(size_t asize);
static void place(void *bp, size_t asize);

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void)
{   
    // mem_sbrk: 힙 영역을 incr(0이 아닌 양수) bytes 만큼 확장하고, 새로 할당된 힙 영역의 첫번째 byte를 가리키는 제네릭 포인터를 리턴함
    /* 비어있는 heap을 만든다.*/
    if((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1){
        return -1;
    };
        
    PUT(heap_listp, 0);                             // Alignment padding
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1));    // Prologue header 
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1));     // Prologue footer
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));          // Epilogue header
    heap_listp += (2*WSIZE); 

    if (extend_heap(CHUNKSIZE/WSIZE) == NULL){
        return -1;
    }

    return 0;
}

    // 두 가지 다른 경우에 호출된다.
    // (1) 힙이 초기화 될때 (2) mm_malloc이 적당한 맞춤fit을 찾지 못했을 때
static void *extend_heap(size_t words)
{
    // 요청한 크기를 인접 2워드의 배수(8바이트)로 반올림하여, 그 후에 추가적인 힙 공간 요청
    char *bp;
    size_t size;
    // 요청한 크기를 2워드의 배수로 반올림하고 추가 힙 공간을 요청함
    size = (words %2) ? (words+1)*WSIZE : words * WSIZE;
    if((long)(bp = mem_sbrk(size)) == -1)
        return NULL;


    PUT(HDRP(bp), PACK(size, 0));  //free 블록의 header 생성
    PUT(FTRP(bp), PACK(size, 0));  //free 블록의 footer 생성
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); // new epilogue header

    return coalesce(bp);
};

// 할당된 블록을 합칠 수 있는 경우 4가지에 따라 메모리 연결
static void *coalesce(void *bp)
{
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));  // 앞블럭의 헤더와 0x1 &연산이 무슨의미? 할당중인지 아닌지 확인용이네
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc){              // Case 1 : 현재만 반환시  둘다 할당중임
        last_modip = bp;
        return bp;
    }
    else if(prev_alloc && !next_alloc){         // Case 2 : 다음 블록과 병합
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));  //크기만큼 건너뛰고 헤더라 생각하고 읽는데 마지막 비트가1이면 할당한거고
        PUT(FTRP(bp), PACK(size, 0));  //0이면 할당아니니까 어떤 데이터가 있든 상관없이 사용..
    }
    else if(!prev_alloc && next_alloc){         // Case 3 : 이전 블록과 병합
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
        bp = PREV_BLKP(bp);
    }
    else{                                       // Case 4 : 이전 블록과 다음 블록 병합
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
        // PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        // PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        // bp = PREV_BLKP(bp);
        bp = PREV_BLKP(bp);
        PUT(HDRP((bp)), PACK(size, 0));
        PUT(FTRP(bp), PACK(size, 0));
    }
    last_modip = bp;
    return bp;
}

/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
void *mm_malloc(size_t size)
{
    size_t asize;  //size만큼 할당하기위해 필요한 헤더나 등등으로 인해 실제로는 이만큼..
    size_t extendsize;
    char *bp;

    if (size == 0){
        return NULL;
    }
    // size를 바탕으로 헤더와 푸터의 공간 확보
    // 8바이트는 정렬조건을 만족하기 위해
    // 추가 8바이트는 헤더와 푸터 오버헤드를 위해서 확보
    if (size <= DSIZE){
        asize = 2*DSIZE;
    }else{
        asize = DSIZE*((size+(DSIZE) + (DSIZE-1)) / DSIZE);
    }

    // 가용 블록을 가용리스트에서 검색하고 할당기는 요청한 블록을 배치한다.
    if((bp = find_fit(asize)) !=NULL){
        place(bp, asize);
        return bp;
    }

    //맞는 블록을 찾기 못한다면 새로운 가용 블록으로 확장하고 배치한다.
    extendsize = MAX(asize, CHUNKSIZE);
    if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
        return NULL;
    place(bp, asize);

    last_modip = bp;
    return bp;
}

/*
 * mm_free - Freeing a block does nothing.
 */
void mm_free(void *ptr)
{
    size_t size = GET_SIZE(HDRP(ptr));

    PUT(HDRP(ptr), PACK(size, 0));
    PUT(FTRP(ptr), PACK(size, 0));
    coalesce(ptr);
}

// static void *find_fit(size_t asize){
//     // 적절한 가용 블록을 검색하고 가용블록의 주소를 반환한다
//     //first fit 검색을 수행한다. -> 리스트 처음부터 탐색하여 가용블록 찾기
//     void *bp;
//     //헤더의 사이즈가 0보다 크다. -> 에필로그까지 탐색한다.
//     for (bp = heap_listp; GET_SIZE(HDRP(bp)) >0; bp = NEXT_BLKP(bp)){
//         if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){
//             return bp;
//         }
//     }
//     return NULL;
// }

static void *find_fit(size_t asize){
    void *bp;
    for (bp = last_modip; GET_SIZE(HDRP(bp)) >0; bp = NEXT_BLKP(bp)){
        if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){
            last_modip = bp;
            return bp;
        }
    }
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) >0; bp = NEXT_BLKP(bp)){
        if(!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){
            last_modip = bp;
            return bp;
        }
    }
    return NULL;
}


// 
static void place(void *bp, size_t asize){
    // 맞는 블록을 찾으면 요청한 블록을 배치하고 초과부분을 분할한다.
    size_t csize = GET_SIZE(HDRP(bp));

    if((csize - asize) >= (2*DSIZE)){
        //가용 블록에 사이즈 - 요청한 블록의 사이즈 각 더블워드*2 크거나 같을때
        //요청 블록을 넣고 남은 사이즈는 가용 블록으로 분할
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        bp = NEXT_BLKP(bp);
        PUT(HDRP(bp), PACK(csize - asize, 0));
        PUT(FTRP(bp), PACK(csize - asize, 0));
    }else{
        //할당하고 남은 블록이 더블워드*2보다 작다며 나누지 않고 할당
        // 남은 블록이 더블워드*2보다 작은 경우는 데이터를 담을 수 없음
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
    }
    last_modip = bp;
}

/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *bp, size_t size)
{
    void *old_bp = bp;
    void *new_bp;
    size_t copySize;
    
    new_bp = mm_malloc(size);
    if (new_bp == NULL)
      return NULL;
    copySize = GET_SIZE(HDRP(old_bp));
    if (size < copySize)
      copySize = size;
    memcpy(new_bp, old_bp, copySize);  // 메모리의 특정한 부분으로부터 얼마까지의 부분을 다른 메모리 영역으로 복사해주는 함수(old_bp로부터 copySize만큼의 문자를 new_bp로 복사해라)
    mm_free(old_bp);
    last_modip = new_bp;
    return new_bp;
}
// /*
//  * mm-naive.c - The fastest, least memory-efficient malloc package.
//  * 
//  * In this naive approach, a block is allocated by simply incrementing
//  * the brk pointer.  A block is pure payload. There are no headers or
//  * footers.  Blocks are never coalesced or reused. Realloc is
//  * implemented directly using mm_malloc and mm_free.
//  *
//  * NOTE TO STUDENTS: Replace this header comment with your own header
//  * comment that gives a high level description of your solution.
//  */
// #include <stdio.h>
// #include <stdlib.h>
// #include <assert.h>
// #include <unistd.h>
// #include <string.h>

// #include "mm.h"
// #include "memlib.h"

// /*********************************************************
//  * NOTE TO STUDENTS: Before you do anything else, please
//  * provide your team information in the following struct.
//  ********************************************************/
// team_t team = {
//     /* Team name */
//     "ateam",
//     /* First member's full name */
//     "Harry Bovik",
//     /* First member's email address */
//     "bovik@cs.cmu.edu",
//     /* Second member's full name (leave blank if none) */
//     "",
//     /* Second member's email address (leave blank if none) */
//     ""
// };

// /* single word (4) or double word (8) alignment */
// #define ALIGNMENT 8

// /* rounds up to the nearest multiple of ALIGNMENT */
// #define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


// #define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

// #define WSIZE 4
// #define DSIZE 8
// #define CHUNKSIZE (1<<12)

// #define MAX(x, y) ((x) > (y) ? (x) : (y))

// #define PACK(size, alloc) ((size) | (alloc))

// #define GET(p) (*(unsigned int *)(p))
// #define PUT(p, val) (*(unsigned int *)(p) = (val))

// #define GET_SIZE(p) (GET(p) & ~0x7)
// #define GET_ALLOC(p) (GET(p) & 0x1)

// #define HDRP(bp) ((char *)(bp) - WSIZE)
// #define FTRP(bp) ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

// #define NEXT_BLKP(bp) ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE)))
// #define PREV_BLKP(bp) ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE)))

// static char *heap_listp;




// /* 
//  * mm_init - initialize the malloc package.
//  */
// int mm_init(void)
// {
//     if ((heap_listp = mem_sbrk(4*WSIZE)) == (void*)-1){ // old brk에서 4만큼 늘려서 mem brk로 늘림.
//         return -1;
//     }
//     PUT(heap_listp,0); // 블록생성시 넣는 padding을 한 워드 크기만큼 생성. heap_listp 위치는 맨 처음.
//     PUT(heap_listp + (1*WSIZE), PACK(DSIZE,1)); // prologue header 생성. pack을 해석하면, 할당을(1) 할건데 8만큼 줄거다(DSIZE). -> 1 WSIZE 늘어난 시점부터 PACK에서 나온 사이즈를 줄거다.)
//     PUT(heap_listp + (2*WSIZE), PACK(DSIZE,1)); // prologue footer생성.
//     PUT(heap_listp + (3*WSIZE), PACK(0,1)); // epilogue block header를 처음에 만든다. 그리고 뒤로 밀리는 형태.
//     heap_listp+= (2*WSIZE); // prologue header와 footer 사이로 포인터로 옮긴다. header 뒤 위치. 다른 블록 가거나 그러려고.

//     if (extend_heap(CHUNKSIZE/WSIZE)==NULL) // extend heap을 통해 시작할 때 한번 heap을 늘려줌. 늘리는 양은 상관없음.
//         return -1;
//     return 0;
// }

// /* 
//  * mm_malloc - Allocate a block by incrementing the brk pointer.
//  *     Always allocate a block whose size is a multiple of the alignment.
//  */
// void *mm_malloc(size_t size)
// {
//     size_t asize;
//     size_t extendsize;
//     char *bp;

//     if (size == 0){
//         return NULL;
//     }
//     // size를 바탕으로 헤더와 푸터의 공간 확보
//     // 8바이트는 정렬조건을 만족하기 위해
//     // 추가 8바이트는 헤더와 푸터 오버헤드를 위해서 확보
//     if (size <= DSIZE){
//         asize = 2*DSIZE;
//     }else{
//         asize = DSIZE*((size+(DSIZE) + (DSIZE-1)) / DSIZE);
//     }

//     // 가용 블록을 가용리스트에서 검색하고 할당기는 요청한 블록을 배치한다.
//     if((bp = find_fit(asize)) !=NULL){
//         place(bp, asize);
//         return bp;
//     }

//     //맞는 블록을 찾기 못한다면 새로운 가용 블록으로 확장하고 배치한다.
//     extendsize = MAX(asize, CHUNKSIZE);
//     if ((bp = extend_heap(extendsize/WSIZE)) == NULL)
//         return NULL;
//     place(bp, asize);

//     return bp;
// }

// /*
//  * mm_free - Freeing a block does nothing.
//  */
// void mm_free(void *ptr)
// {
//     size_t size = GET_SIZE(HDRP(ptr));
		
// 		// 헤더와 푸터를 0으로 할당하고 coalesce를 호출하여 가용 메모리를 이어준다.
//     PUT(HDRP(ptr), PACK(size, 0)); 
//     PUT(FTRP(ptr), PACK(size, 0));
//     coalesce(ptr);
// }

// /*
//  * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
//  */
// void *mm_realloc(void *ptr, size_t size)
// {
//     void *oldptr = ptr;
//     void *newptr;
//     size_t copySize;
    
//     newptr = mm_malloc(size);
//     if (newptr == NULL)
//       return NULL;
//     copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
//     if (size < copySize)
//       copySize = size;
//     memcpy(newptr, oldptr, copySize);
//     mm_free(oldptr);
//     return newptr;
// }

// static void *extend_heap(size_t words)
// {
//     // 요청한 크기를 인접 2워드의 배수(8바이트)로 반올림하여, 그 후에 추가적인 힙 공간 요청
//     char *bp;
//     size_t size;
//     // 요청한 크기를 2워드의 배수로 반올림하고 추가 힙 공간을 요청함
//     size = (words %2) ? (words+1)*WSIZE : words * WSIZE;
//     if((long)(bp = mem_sbrk(size)) == -1)
//         return NULL;


//     PUT(HDRP(bp), PACK(size, 0));
//     PUT(FTRP(bp), PACK(size, 0));
//     PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));

//     return coalesce(bp);
// }

// static void *coalesce(void *bp)
// {
//     size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
//     size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
//     size_t size = GET_SIZE(HDRP(bp));

//     if (prev_alloc && next_alloc){              // Case 1 : 현재만 반환시
//         return bp;
//     }
//     else if(prev_alloc && !next_alloc){         // Case 2 : 다음 블록과 병합
//         size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
//         PUT(HDRP(bp), PACK(size, 0));
//         PUT(FTRP(bp), PACK(size, 0));
//     }
//     else if(!prev_alloc && next_alloc){         // Case 3 : 이전 블록과 병합
//         size += GET_SIZE(HDRP(PREV_BLKP(bp)));
//         PUT(FTRP(bp), PACK(size, 0));
//         PUT(HDRP(PREV_BLKP(bp)), PACK(size,0));
//         bp = PREV_BLKP(bp);
//     }
//     else{                                       // Case 4 : 이전 블록과 다음 블록 병합
//         size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp)));
//         PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
//         PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
//         bp = PREV_BLKP(bp);
				
// 				//bp = PREV_BLKP(bp);             동일하게 동작함
//         //PUT(HDRP((bp)), PACK(size, 0));
//         //PUT(FTRP(bp), PACK(size, 0));
//     }
//     return bp;
// }

// static void place(void *bp, size_t asize){
//     size_t csize = GET_SIZE(HDRP(bp));

//     if((csize - asize) >= (2*DSIZE)){
//         PUT(HDRP(bp), PACK(asize, 1));
//         PUT(FTRP(bp), PACK(asize, 1));
//         bp = NEXT_BLKP(bp);
//         PUT(HDRP(bp), PACK(csize- asize, 0));
//         PUT(FTRP(bp), PACK(csize- asize, 0));
//     }else{
//         PUT(HDRP(bp), PACK(csize, 1));
//         PUT(FTRP(bp),PACK(csize, 1));
//     }
// }












//fasdfasdfa