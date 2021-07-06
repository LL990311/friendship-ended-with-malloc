#include "virtual_alloc.h"

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>

#define BITS_PER_BYTE 8
#define BUDDY_ALLOCATOR_STRUCTURE_SIZE 40

void* virtual_heap = NULL;

void* virtual_sbrk(int32_t increment) { return sbrk(increment); }

void simple_test1() {
    fprintf(stdout, "SIMPLE_TEST1\n");
    // Reset heap
    brk(virtual_heap);

    init_allocator(virtual_heap, 15, 12);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");

    virtual_malloc(virtual_heap, 8000);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
    virtual_malloc(virtual_heap, 8000);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
    virtual_malloc(virtual_heap, 8000);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
}

void simple_test2() {
    fprintf(stdout, "SIMPLE_TEST2\n");
    // Reset heap
    brk(virtual_heap);

    init_allocator(virtual_heap, 15, 12);
    virtual_malloc(virtual_heap, 8000);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
    virtual_malloc(virtual_heap, 10000);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
    virtual_malloc(virtual_heap, 8000);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
}

// Test case showed in the assignment doc.
void simple_test3() {
    fprintf(stdout, "SIMPLE_TEST3\n");
    // Reset heap
    brk(virtual_heap);

    init_allocator(virtual_heap, 15, 12);
    void* ptr = virtual_malloc(virtual_heap, 8000);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
    virtual_malloc(virtual_heap, 10000);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
    virtual_free(virtual_heap, ptr);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
}

// Test shrink
void simple_test4() {
    fprintf(stdout, "SIMPLE_TEST4\n");
    // Reset heap
    brk(virtual_heap);

    int init_size = 15;
    int min_size = 12;

    init_allocator(virtual_heap, init_size, min_size);
    void* ptr = virtual_malloc(virtual_heap, 8000);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
    void* ptr2 = virtual_malloc(virtual_heap, 10000);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
    virtual_free(virtual_heap, ptr);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
    virtual_free(virtual_heap, ptr2);
    // Since all memories have been freed, the heap should shrink
    // void* end = sbrk(0);
    // assert(end <= virtual_heap + BUDDY_ALLOCATOR_STRUCTURE_SIZE +
    //                   (1 << (init_size - min_size + 1)) / BITS_PER_BYTE + 1);
}

void simple_test5() {
    fprintf(stdout, "SIMPLE_TEST5\n");
    // Reset heap
    brk(virtual_heap);

    init_allocator(virtual_heap, 15, 12);
    virtual_info(virtual_heap);
    fprintf(stdout, "------------------------\n");
    assert(virtual_malloc(virtual_heap, 32768) != NULL);
    virtual_info(virtual_heap);
}

int main() {
    srand((unsigned int)time(NULL));
    // srand(0);

    virtual_heap = sbrk(0);

    simple_test1();
    simple_test2();
    simple_test3();
    simple_test4();
    simple_test5();

    fprintf(stdout, "RANDOM_TEST\n");
    int i;
    for (i = 0; i < 100; i++) {
        random_test();
    }

    fprintf(stdout, "All tests passed!\n");

    return 0;
}
