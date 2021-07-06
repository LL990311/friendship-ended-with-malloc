#include "virtual_alloc.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

#define BLK_FREE false
#define BLK_ALLOC true

#define BITS_PER_BYTE 8
#define BITS_PER_STATUS (sizeof(uint8_t) * BITS_PER_BYTE)
#define PROTECT_OFFSET 0

// Left child block
#define LEFT(blk) (((blk) << 1) + 1)
// Parent block
#define PARENT(blk) (((blk)-1) >> 1)

typedef struct {
    // Store the initial info
    uint8_t initial_size;
    uint8_t min_size;
    uint32_t num_blocks;
    void* cur_break;           // program brk
    uint32_t last_used_block;  // when the last used block is freed, a heap
    // shrink occurs
    bool allow_shrink;  // used for realloc to tell virtual_free not to shrink
    void* temp_break;   // cached shrink
    // Address of the real heap start for user
    // basicly, start = heapstart + buddy_allocator_data_structures_size
    void* start;
    // Whether a block is free or allocated, use bit map and binary heap
    // representation.
    // For example,
    //      0 -> the initial block
    //      1 -> left half block of the initial block
    //      2 -> right half block of the initial block
    //      3 -> left half block of 1
    // Block 1 and 2 are buddy blocks.
    // If two chilren of a block are free, that block is a whole, not split.
    // If a block is split, that block is marked as allocated.
    uint8_t status[0];
} heap_t;

// Defined in other .c file.
extern void* virtual_sbrk(int32_t increment);

// Function prototypes

void block_alloc(heap_t* heap, uint32_t n);
void block_free(heap_t* heap, uint32_t n);
bool block_status(heap_t* heap, uint32_t n);
bool block_is_left(uint32_t n);
uint8_t my_log2(uint32_t n);
uint32_t find_fit(heap_t* heap, uint8_t sz);
uint8_t block_sz(heap_t* heap, uint32_t blk);
uint32_t block_split(heap_t* heap, uint32_t blk, uint8_t expected_sz);
void* block_to_ptr(heap_t* heap, uint32_t blk);
uint32_t ptr_to_block(heap_t* heap, void* ptr);
void virtual_info_r(heap_t* heap, uint32_t blk, uint32_t blk_size);
void* my_virtual_sbrk(int32_t increment);

/*==========================================================================*/

// Initialize the buddy allocator
void init_allocator(void* heapstart, uint8_t initial_size, uint8_t min_size) {
    uint32_t status_size =
            (1 << (initial_size - min_size + 1)) / BITS_PER_STATUS + 1;
    uint32_t heap_size = sizeof(heap_t) + status_size;

    // Initialize buddy allocator heap data structure
    my_virtual_sbrk(heap_size);
    // assert(heapstart == heapstart2);

    heap_t* heap = (heap_t*)heapstart;
    heap->initial_size = initial_size;
    heap->min_size = min_size;
    heap->num_blocks = (1 << (initial_size - min_size + 1)) - 1;
    heap->start = heapstart + heap_size;
    heap->cur_break = heapstart + heap_size;
    heap->last_used_block = heap->num_blocks;
    heap->allow_shrink = true;
    heap->temp_break = NULL;

    // Mark all blocks as free
    memset(heap->status, 0, status_size);

    // assert((void*)heap->status + status_size <= virtual_sbrk(0));
}

void* virtual_malloc(void* heapstart, uint32_t size) {
    if (size == 0) return NULL;

    heap_t* heap = (heap_t*)heapstart;

    // sz means 2^(sz-1) < size <= 2^sz
    uint8_t sz = my_log2(size);
    if (sz > heap->initial_size) {
        return NULL;
    }
    if (sz < heap->min_size) {
        sz = heap->min_size;
    }

    // Find a suitable block
    uint32_t blk = find_fit(heap, sz);
    if (blk == 0 && block_status(heap, 0) == BLK_ALLOC) {
        return NULL;  // do not find a suitable block
    }

    // Split the block if needed
    blk = block_split(heap, blk, sz);

    // ptr is the starting address of the block
    void* ptr = block_to_ptr(heap, blk);
    // end is the ending address
    void* end = ptr + ((uint32_t)1 << sz);
    // Since this block is going to return to the user,
    // Check if the current brk covers this region,
    // If not, extend the heap
    if (heap->cur_break < end) {
        // Extend the heap
        if (my_virtual_sbrk(end - heap->cur_break) == (void*)-1) {
            virtual_free(heapstart, ptr);
            return NULL;
        }
        // Update current brk and the last used block
        heap->cur_break = end;
        heap->last_used_block = blk;
    }

    return ptr;
}

int virtual_free(void* heapstart, void* ptr) {
    heap_t* heap = (heap_t*)heapstart;

    if (ptr == NULL) {
        return 1;  // NULL ptr
    }

    uint32_t blk = ptr_to_block(heap, ptr);
    if (blk >= heap->num_blocks) {
        return 1;  // invalid ptr
    }

    if (block_status(heap, blk) == BLK_FREE) {
        return 1;  // the block is free
    }

    // Mark this block as free
    block_free(heap, blk);

    // Merge free blocks
    uint32_t buddy;
    while (blk > 0) {
        // If the last used block is freed, its parent becomes the new
        // last used block
        if (blk == heap->last_used_block) {
            heap->last_used_block = PARENT(blk);
        }
        // Check if its buddy block is free
        buddy = block_is_left(blk) ? blk + 1 : blk - 1;
        if (block_status(heap, buddy) == BLK_FREE) {
            // If it is free, merge two blocks into one
            blk = PARENT(blk);
            block_free(heap, blk);
        } else {
            break;
        }
    }

    // Check the end brk after freeing
    void* end;
    if (heap->last_used_block == 0 && block_status(heap, 0) == BLK_FREE) {
        heap->last_used_block = heap->num_blocks;
        end = heap->start;
    } else {
        end = block_to_ptr(heap, heap->last_used_block) +
              ((uint32_t)1 << block_sz(heap, heap->last_used_block));
    }

    // Shrink the heap if needed
    if (heap->cur_break > end) {
        if (heap->allow_shrink) {
            // Shrink the heap
            my_virtual_sbrk(end - heap->cur_break);
            heap->cur_break = end;
        } else {
            // While called by realloc, the old ptr may be the last used block
            // So, cache the shrink operation after copying data
            heap->temp_break = end;
        }
    }

    return 0;
}

void* virtual_realloc(void* heapstart, void* ptr, uint32_t size) {
    heap_t* heap = (heap_t*)heapstart;

    if (ptr == NULL) {  // NULL ptr, act as malloc
        return virtual_malloc(heapstart, size);
    }
    if (size == 0) {  // zero size, act as free
        virtual_free(heapstart, ptr);
        return NULL;
    }

    uint8_t new_sz = my_log2(size);
    if (new_sz > heap->initial_size) {
        return NULL;
    }
    if (new_sz < heap->min_size) {
        new_sz = heap->min_size;
    }

    uint32_t blk = ptr_to_block(heap, ptr);
    uint8_t old_sz = block_sz(heap, blk);

    if (new_sz == old_sz) {
        return ptr;
    }

    if (new_sz < old_sz) {
        block_split(heap, blk, new_sz);
        return ptr;
    }

    uint32_t old_size = (uint32_t)1 << old_sz;

    // Do not allow shrink since we may need to copy the data
    heap->allow_shrink = false;

    if (virtual_free(heapstart, ptr) != 0) {
        heap->allow_shrink = true;
        return NULL;  // not a valid pointer
    }
    void* new_ptr = virtual_malloc(heapstart, size);
    if (new_ptr == NULL) {  // allocate failed,
        // Malloc again should return the same ptr
        assert(ptr == virtual_malloc(heapstart, old_size));
        new_ptr = NULL;  // return the old ptr
    } else if (new_ptr != ptr) {
        // Copy data if the new ptr is different with the old
        memcpy(new_ptr, ptr, old_size);
    }

    // Perform the cached shrink operation
    if (heap->temp_break) {
        my_virtual_sbrk(heap->temp_break - heap->cur_break);
        heap->cur_break = heap->temp_break;
        heap->temp_break = NULL;
    }
    heap->allow_shrink = true;

    return new_ptr;
}

// Print information of all blocks.
void virtual_info(void* heapstart) {
    heap_t* heap = (heap_t*)heapstart;
    virtual_info_r(heap, 0, 1 << heap->initial_size);
}

/*==========================================================================*/

// Set block n as allocated.
void block_alloc(heap_t* heap, uint32_t n) {
    heap->status[n / BITS_PER_STATUS] |= ((uint32_t)1 << (n % BITS_PER_STATUS));
}

// Set block n as free.
void block_free(heap_t* heap, uint32_t n) {
    heap->status[n / BITS_PER_STATUS] &=
            ~((uint32_t)1 << (n % BITS_PER_STATUS));
}

// Get allocation status of block n.
bool block_status(heap_t* heap, uint32_t n) {
    return heap->status[n / BITS_PER_STATUS] &
           ((uint32_t)1 << (n % BITS_PER_STATUS));
}

// Check if block n is the left child of its parent.
bool block_is_left(uint32_t n) {
    // All left children are odd
    return n & 1;
}

// Simple log2 for integer value.
uint8_t my_log2(uint32_t n) {
    uint8_t i;
    for (i = 0; i < 32; i++) {
        if (n <= ((uint32_t)1 << i)) {
            return i;
        }
    }
    return 0;
}

// Find a suitable block.
// If not found, return 0.
uint32_t find_fit(heap_t* heap, uint8_t sz) {
    // Start block
    uint32_t start = (1 << ((uint32_t)heap->initial_size - sz)) - 1;
    // End block
    uint32_t end = start << 1;
    uint32_t i;
    assert(end < heap->num_blocks);
    for (i = start; i < end; i += 2) {  // search from start to end
        // If the status of these two buddy blocks are different,
        // that means their parent block has been split,
        // and the free block of the two is a suitable block to return
        if (block_status(heap, i) != block_status(heap, i + 1)) {
            return block_status(heap, i) == BLK_FREE ? i : i + 1;
        }
    }
    // If not found, try to find a bigger one
    return sz < heap->initial_size ? find_fit(heap, sz + 1) : 0;
}

// Return the size of a block.
// Note that the real size of the block should be 1 << ret_value.
uint8_t block_sz(heap_t* heap, uint32_t blk) {
    uint8_t sz = heap->initial_size;
    while (blk > 0) {
        blk = PARENT(blk);
        sz--;
    }
    return sz;
}

// Split a block.
uint32_t block_split(heap_t* heap, uint32_t blk, uint8_t expected_sz) {
    uint8_t sz = block_sz(heap, blk);
    assert(expected_sz <= sz);

    // Mark the block as allocated
    block_alloc(heap, blk);

    // Split the block to expected size
    uint8_t i;
    for (i = sz; i > expected_sz; i--) {
        blk = LEFT(blk);         // choose the left child
        block_alloc(heap, blk);  // mark the left child as allocated
    }

    return blk;
}

// Get the block starting address in heap.
void* block_to_ptr(heap_t* heap, uint32_t blk) {
    // For example, if given blk = 2, 2 is the right child of 0, so the starting
    // address of 2 is starting_address_of_0 + half_of_size_of_0.
    uint32_t offset = 0;
    uint32_t size = ((uint32_t)1 << block_sz(heap, blk));
    while (blk > 0) {
        // Once go right, add half of the size
        if (!block_is_left(blk)) {
            offset += size;
        }
        blk = PARENT(blk);
        size <<= 1;
    }
    return heap->start + offset;
}

// Get the block number based on the starting address.
uint32_t ptr_to_block(heap_t* heap, void* ptr) {
    if (ptr < heap->start) {
        return heap->num_blocks;  // Invalid ptr
    }
    uint32_t offset = ptr - heap->start;

    if (offset > (1 << heap->initial_size) || offset % (1 << heap->min_size)) {
        // Out of range or not a valid block starting address
        return heap->num_blocks;
    }

    // Block 0 and block 1, 3, 5 all start from 0 (suppose the block start at 0
    // in heap). For example, given a ptr 0, first find the upper block starts
    // at ptr, which is 0
    uint32_t blk = 0;
    uint32_t size = ((uint32_t)1 << heap->initial_size);
    while (offset > 0 && size > 0) {
        size >>= 1;
        if (offset >= size) {
            blk = LEFT(blk) + 1;
            offset -= size;
        } else {
            blk = LEFT(blk);
        }
    }

    // Its left child has the same starting address,
    // If the left child is allocated, choose the left child since this block
    // has been split
    uint32_t left = LEFT(blk);
    while (left < heap->num_blocks && block_status(heap, left) == BLK_ALLOC) {
        blk = left;
        left = LEFT(blk);
    }

    return blk;
}

// Print information of all blocks.
// This is a recursive function.
void virtual_info_r(heap_t* heap, uint32_t blk, uint32_t blk_size) {
    // If one of this chilren is allocated, that means this block has been
    // split, ignore this block and go deeper to its children blocks
    uint32_t left = LEFT(blk);
    if (left < heap->num_blocks &&
        (block_status(heap, left) == BLK_ALLOC ||
         block_status(heap, left + 1) == BLK_ALLOC)) {
        virtual_info_r(heap, left, blk_size >> 1);
        virtual_info_r(heap, left + 1, blk_size >> 1);
    } else {
        // Else, this block has not been split, print its status
        if (block_status(heap, blk) == BLK_FREE) {
            fprintf(stdout, "free %d\n", blk_size);
        } else {
            fprintf(stdout, "allocated %d\n", blk_size);
        }
    }
}

void* my_virtual_sbrk(int32_t increment) {
    return virtual_sbrk(increment + PROTECT_OFFSET);
}
