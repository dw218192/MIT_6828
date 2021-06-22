#ifndef JOS_INC_KMALLOC_H
#define JOS_INC_KMALLOC_H

extern char kheap[];
void* kmalloc(uint32_t nbytes);
void kfree(void* ptr);

#endif