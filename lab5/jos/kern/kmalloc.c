#include <inc/malloc.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <kern/pmap.h>
#include <inc/x86.h>

// kernel virtual memory allocator

// fixed kernel heap for dynamic memory allocation
// does not make use of page_alloc for simplicity

// mapped to vm UHEAP in kernel address space
char kheap[KHEAPSIZE]
__attribute__ ((aligned(PGSIZE)));

static uintptr_t kheaptop = (uintptr_t) kheap;

static void* kheap_sbrk(intptr_t nbytes)
{
    uintptr_t ret = kheaptop;
    if(kheaptop + nbytes < (uintptr_t) kheap || kheaptop + nbytes >= (uintptr_t) kheap + KHEAPSIZE)
        panic("kheap_sbrk: allocation out of bounds\n");
    
    kheaptop += nbytes;
    return (void*)ret;
}

void* kmalloc(uint32_t nbytes)
{
    return _malloc(kheap_sbrk, nbytes);
}

void kfree(void* ptr)
{
    _free(ptr);
}