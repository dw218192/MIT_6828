#ifndef JOS_INC_MALLOC_H
#define JOS_INC_MALLOC_H

#include<inc/types.h>

// lib/malloc.c
void _free(void *ap);
void* _malloc(void* (sbrk)(intptr_t), uint32_t nbytes);

#endif