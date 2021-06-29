#ifndef JOS_INC_MALLOC_H
#define JOS_INC_MALLOC_H

#include <inc/types.h>

void* _malloc(void* (sbrk)(intptr_t), uint32_t nbytes);

void _free(void* ap);

#endif