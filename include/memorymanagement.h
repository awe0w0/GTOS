#ifndef __GTOS__MEMORYMANAGEMENT_H
#define __GTOS__MEMORYMANAGEMENT_H

#include <common/types.h>

namespace gtos {
    //链表结构
    struct MemoryChunk {
        MemoryChunk* next;
        MemoryChunk* prev;
        bool allocated;
        size_t size;
    };

    class MemoryManager {    
    protected:
        MemoryChunk* first;
    public:
        static MemoryManager* activeMemoryManager;

        MemoryManager(size_t first, size_t size);
        ~MemoryManager();

        void* malloc(size_t size);
        void free(void* ptr);
    };
}

void* operator new(unsigned size);
void* operator new[](unsigned size);

void* operator new(unsigned size, void* ptr);
void* operator new[](unsigned size, void* ptr);

void operator delete(void* ptr);
void operator delete[](void* ptr);

#endif