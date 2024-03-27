/*
 * mm.c
 *
 * Name: Glen Jiang
 *
 * checkpoint 1
 * the init function creates the prologue and epilogue, which is 16 bytes and 8 bytes and also includes 8 bytes of padding
 * i created a header struct with 4 bits of alloc and 60 bits of size
 * the malloc function has multiple cases, i first extended the heap, to find free blocks i iterated through in a line
 * the free function also has multiple cases and i added coalescing if possible
 * for realloc i used the malloc and free functions from before, malloc and then free
 * 
 * checkpoint 2
 * i implemented a segregated freelist for free blocks
 * the freelist is explicit and included in each free block, allocated blocks do not have a explicit space for a list
 * the freelist is a double linked list so that we can tell next and previous free blocks
 * items placed into the freelist are always placed at the front, and if a freelist array is full, my code checks the next freelist of a bigger size
 * freelists are separated first by 16s starting at 32 then by powers of 2 onwards
 * 
 * final submission
 * implemented footer optimization, so that we do not need a footer anymore for allocated blocks
 * header alloc bits now show if the previous block is free or not
 * changed alignedsize calculation so that the allocated size would always be a multiple of 16
 * make sure minimum size is 32
 * 
 * Also, read the README carefully and in its entirety before beginning.
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#include "mm.h"
#include "memlib.h"

/*
 * If you want to enable your debugging output and heap checker code,
 * uncomment the following line. Be sure not to have debugging enabled
 * in your final submission.
 */
#define DEBUG

#ifdef DEBUG
// When debugging is enabled, the underlying functions get called
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#else
// When debugging is disabled, no code gets generated
#define dbg_printf(...)
#define dbg_assert(...)
#endif // DEBUG

// do not change the following!
#ifdef DRIVER
// create aliases for driver tests
#define malloc mm_malloc
#define free mm_free
#define realloc mm_realloc
#define calloc mm_calloc
#define memset mm_memset
#define memcpy mm_memcpy
#endif // DRIVER

#define ALIGNMENT 16

// rounds up to the nearest multiple of ALIGNMENT
static size_t align(size_t x)
{
    return ALIGNMENT * ((x+ALIGNMENT-1)/ALIGNMENT);
}

struct Header {
    uint64_t alloc:4;
    uint64_t size:60;
};

struct list {
    struct list *prev;
    struct list *next;
};

#define NUM_FREELIST 13
// 4 - 57.2% 64, 256, 1024
// 12 - 59.6%
// 15 - 59.7%

struct list * freelists[NUM_FREELIST];

int selectfreelist (size_t s) {
    if (s <= 32) {
	return 0;
    } else if (s <= 48) {
	return 1;
    } else if (s <= 64) {
	return 2;
    } else if (s <= 128) {
	return 3;
    } else if (s <= 256) {
	return 4;
    } else if (s <= 512) {
	return 5;
    } else if (s <= 1024) {
	return 6;
    } else if (s <= 2048) {
	return 7;
    } else if (s <= 4096) {
	return 8;
    } else if (s <= 8192) {
	return 9;
    } else if (s <= 16384) {
	return 10;
    } else if (s <= 32768) {
	return 11;
    } else {
	return 12;
    }
}

/*
 * mm_init: returns false on error, true on success.
 */
bool mm_init(void)
{
    // IMPLEMENT THIS
    // initialize prologue
    struct Header *header = (struct Header *) mm_sbrk(4*sizeof(struct Header));
    if (header == NULL) {
	return false;
    }
    header++;
    header->size = 0x1;
    header->alloc = 0x1;
    header++;
    header->size = 0x1;
    header->alloc = 1;
    
    // initialize epilogue
    header++;
    header->size = 0;
    header->alloc = 1;
    
    // set freelist to NULL
    for (int i = 0; i < NUM_FREELIST; i++) {
	freelists[i] = NULL;
    }
    return true;
}

/*
 * malloc
 */
void* malloc(size_t size)
{
    // IMPLEMENT THIS
    // search free space, skip prologue
    char *p = (char *) mm_heap_lo();
    struct Header *headp = (struct Header *) p;
    headp = headp+3;
    char *returnp = NULL;
    size_t alignedsize = align(size+8);
    if (alignedsize < 32) {				// make sure size of allocation is at least 32
	alignedsize = 32;
    }
    int index = selectfreelist(alignedsize);
    if (headp->size == 0) {				// if epilogue
	void *m = mm_sbrk(alignedsize);
	if (m == (void *) -1) {
	    return NULL;
	}
        p = (char *) headp;
	headp = headp+(alignedsize)/8;	// epilogue
	headp->size = 0;
	headp->alloc = 1;
	headp = (struct Header *) p;
	headp->size = (alignedsize)/16;	// free space header
	headp->alloc = 1;
	returnp = (char *) headp + 8;
   } else {
	struct Header *curr = NULL;
	char * fp = NULL;
	while (index < NUM_FREELIST) {		// go through freelists to find free space starting from the smallest possible
	    if (freelists[index] != NULL) {
	        fp = (char *) freelists[index];
	        fp = fp - 8;
	        curr = (struct Header *) fp;
	    } else {
	        index++;
	        continue;
	    }
	    while (curr != NULL && curr->size < (alignedsize)/16) {	// traverse individual freelist
	        fp = (char *) curr + 8;
	        struct list * lip = (struct list *) fp;
	        if (lip->next == NULL) {
		    curr = NULL;			// at end of freelist
		    break;
	        }
	        fp = (char *) lip->next - 8;
	        curr = (struct Header *) fp;
	    }
	    if (curr != NULL) {
	        break;
	    }
	    index++;
	}
	if (curr == NULL) {
	    //epilogue, need extend heap
	    char *m = (char *) mm_sbrk(alignedsize);
	    if (m == (char *) -1) {
		return NULL;
	    }
	    headp = (struct Header *) (m - 8);	// go back previous epilogue
	    p = (char *) headp;
	    headp = headp+(alignedsize)/8;	// go to new epilogue
	    headp->size = 0;
	    headp->alloc = 1;
	    headp = (struct Header *) p;	// go to previous epilogue or new header
	    headp->size = (alignedsize)/16;
	    headp->alloc = 1;
	    returnp = (char *) headp + 8;
	} else if (curr->size == (alignedsize)/16 || curr->size == (alignedsize+16)/16) {
	    // exactly enough space or if space has 16 more bytes
	    if (curr->size == (alignedsize+16)/16) {			// give 16 more since smallest allocation is 32
		alignedsize += 16;
	    }
	    headp = curr;
	    headp->alloc = 1;
	    returnp = (char *) headp + 8;
	    headp = headp+(alignedsize)/8;
	    if (headp->alloc == 3) {			// change next block to show prev block is not free anymore
		headp->alloc = 1;
	    }
	    char * p = (char *) curr;
	    p = p + 8;
	    struct list * lp = (struct list *) p;
	    if (lp->prev != NULL) {
	    	lp->prev->next = lp->next;	// unlink curr from freelist
	    } 
	    if (lp->next != NULL) {
		lp->next->prev = lp->prev;
	    }
	    if (lp == freelists[index]) {
		freelists[index] = lp->next;
	    }
	    lp->next = NULL;
	    lp->prev = NULL;
	} else {
	    // more than enough space split free block into 2
	    headp = curr;
	    uint64_t oldsize = headp->size;
	    headp->size = (alignedsize)/16;
	    headp->alloc = 1;
	    returnp = (char *) headp + 8;
	    size_t s = headp->size;
	    headp = headp + (alignedsize)/8;
	    
	    // initialize new header
	    headp->alloc = 0;
	    headp->size = oldsize - s;
	    p = (char *) headp;
	    char * p2 = p;
	    p += headp->size*16 - 8;
	    headp = (struct Header *) p;
	    headp->size = oldsize - s;
	    headp->alloc = 0;
	    p2 = p2 + 8;
	    struct list * lp = (struct list *) p2;
	    char * pc = (char *) curr;
	    pc = pc + 8;
	    struct list * lpc = (struct list *) pc;    
	    
	    // remove from freelists[index]
	    if (lpc->next != NULL) {
		lpc->next->prev = lpc->prev;
	    }
	    if (lpc->prev != NULL) {
		lpc->prev->next = lpc->next;
	    }
	    if (freelists[index] == lpc) {
		freelists[index] = lpc->next;
	    }
	    lpc->prev = NULL;
	    lpc->next = NULL;
	    int newindex = selectfreelist(16*(oldsize - s));
	    if (freelists[newindex] == NULL) {				// put new free space into freelists
		freelists[newindex] = lp;
		lp->prev = NULL;
		lp->next = NULL;
	    } else {
		lp->next = freelists[newindex];
		freelists[newindex]->prev = lp;
		freelists[newindex] = lp;
		lp->prev = NULL;
	    }
	}
    }
    return returnp;
}

/*
 * free
 */
void free(void* ptr)
{
    // IMPLEMENT THIS
    if (ptr == NULL) {
	return;
    }
    char *p = (char *) ptr;
    p = p - 8;
    struct Header * headp = (struct Header *) p;
    int prevfree = (headp->alloc - 1)/2;	// prevfree shows if prev was free
    headp->alloc = 0;
    uint64_t sum = headp->size;
    char *footerp = p + headp->size*16 - 8;
    struct list * lp = NULL;
    //coalescing going backwards
    if (prevfree) {
	headp = headp - 1;			// go back to previous free space footer
	headp = headp - headp->size*2 + 1;
	sum += headp->size;
	int index = selectfreelist(headp->size*16);
	// remove from free list
	p = (char *) headp;
	p = p + 8;
	lp = (struct list *) p;
	if (lp == freelists[index]) {
	    freelists[index] = lp->next;	//update freelist header
	}
	if (lp->prev != NULL) {
	    lp->prev->next = lp->next;
	}
	if (lp->next != NULL) {
	    lp->next->prev = lp->prev;
	}
	lp->next = NULL;
	lp->prev = NULL;
    }
    char *pfront = (char *) headp;
    headp = (struct Header *) footerp;
    headp->alloc = 0;
    headp++;
    //coalescing going forwards
    if (headp->alloc == 0) {
	p = (char *) headp;
	p = p + 8;
	headp = headp + 2*headp->size;
	sum += (headp-1)->size;
	int index = selectfreelist((headp-1)->size*16);
	lp = (struct list *) p;
	if (lp == freelists[index]) {		// update freelist
	    freelists[index] = lp->next;
	}
	if (lp->prev != NULL) {
	    lp->prev->next = lp->next;
	}
	if (lp->next != NULL) {
	    lp->next->prev = lp->prev;
	}
	lp->next = NULL;
	lp->prev = NULL;
    }
    headp->alloc = 3;				// make next block say prev block is free
    headp = headp - 1;
    headp->size = sum;
    headp = (struct Header *) pfront;
    headp->size = sum;

    // insert into free list
    int index = selectfreelist(sum*16);
    headp++;
    lp = (struct list *) headp;
    if (freelists[index] == NULL) {
	freelists[index] = lp;
	lp->prev = NULL;
	lp->next = NULL;
    } else {
	freelists[index]->prev = lp;
	lp->next = freelists[index];
	lp->prev = NULL;
	freelists[index] = lp;
    }
    return;
}

/*
 * realloc
 */
void* realloc(void* oldptr, size_t size)
{
    // IMPLEMENT THIS
    void* p = NULL;
    if (oldptr == NULL) {
	p = malloc(size);
	return p;
    }
    if (size == 0) {
	free(oldptr);
	return NULL;
    }
    struct Header * headp = (struct Header *) oldptr;
    headp = headp - 1;
    size_t oldsize = headp->size*16;
    size_t copysize = oldsize;
    if (copysize > size) {
	copysize = size;
    }
    //malloc new space, copy data and then free
    p = malloc(size);
    mm_memcpy(p, oldptr, copysize);
    free(oldptr);
    return p;
}

/*
 * calloc
 * This function is not tested by mdriver, and has been implemented for you.
 */
void* calloc(size_t nmemb, size_t size)
{
    void* ptr;
    size *= nmemb;
    ptr = malloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/*
 * Returns whether the pointer is in the heap.
 * May be useful for debugging.
 */
static bool in_heap(const void* p)
{
    return p <= mm_heap_hi() && p >= mm_heap_lo();
}

/*
 * Returns whether the pointer is aligned.
 * May be useful for debugging.
 */
static bool aligned(const void* p)
{
    size_t ip = (size_t) p;
    return align(ip) == ip;
}

/*
 * mm_checkheap
 * You call the function via mm_checkheap(__LINE__)
 * The line number can be used to print the line number of the calling
 * function where there was an invalid heap.
 */
bool mm_checkheap(int line_number)
{
#ifdef DEBUG
    // Write code to check heap invariants here
    // IMPLEMENT THIS
    //check for bit alignment for prologue, bits, size
    char * p = (char *) mm_heap_lo();
    struct Header * headp = (struct Header *) p;
    headp++;
    if (headp->alloc == 0 || headp->size != 1) {
	printf("prologue incorrect");
	return false;							// check 1+2
    }
    headp++;
    if (headp->alloc == 0 || headp->size != 1) {
	printf("prologue incorrect");
	return false;							// check 3+4
    }
    headp++;
    // check allocate flag
    while (headp->size > 0) {
	if (headp->alloc != 0 && headp->alloc != 1 && headp->alloc != 3) {
	    printf("wrong allocation block at %p", headp);
	    return false;						// check 5
	}
	if (headp->alloc == 1 || headp->alloc == 3) {
	    char *p = (char *) (headp+1);
	    if (!aligned(p)) {
		printf("address not aligned at %p", p);
		return false;						// check 6
	    }
	}
	headp += headp->size*2;
    }
    // check freelist
    for (int i = 0; i < NUM_FREELIST; i++) {
	if (freelists[i] == NULL) {
	    continue;
	}
	struct list * lp = freelists[i];
	do {
	    char * p = (char *) freelists[i];
	    p = p - 8;
	    struct Header * headp = (struct Header *) p;
	    if (headp->alloc != 0) {
	        printf ("alloc not set to 1, %p", headp);
		return false;						// check 7
	    }
	    if ((headp + (headp->size*16/8))->alloc != 3) {
		printf("next block header alloc not set to 3, %p", headp);
		return false;						// check 8
	    }
	    printf("%lld\t", (long long) headp->size*16);		
	    lp = lp->next;
	    if (lp > (struct list *) mm_heap_hi()) {
		printf("pointer past max heap, %p", lp);		// check 9
		return false;
	    }
	} while (lp != NULL);
	printf("\n");
    }
    // heap checker
    p = (char *) mm_heap_lo();
    p = p + 24;
    headp = (struct Header *) p;
    while (headp->size != 0) {
	printf("%p, %lld, %d\n", headp, (long long) headp->size*16, headp->alloc);
	if (headp->alloc != 0 && headp->alloc != 1 && headp->alloc != 3) {
	    return false;						// check 10
	}
	p = p + headp->size*16;
	headp = (struct Header *) p;
    }
#endif // DEBUG
    return true;
}