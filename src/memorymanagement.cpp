#include <memorymanagement.h>

using namespace gtos;

void printf(char*);

MemoryManager::MemoryManager(size_t start, size_t size) {

    activeMemoryManager = this;

    if (size < sizeof(MemoryChunk)) {
        first = 0;
    } else {
        first = (MemoryChunk*)start;
        first->allocated = false;
        first->next = 0;
        first->prev = 0;
        first->size = size - sizeof(MemoryChunk);
    }

}

MemoryManager::~MemoryManager() {
    if (activeMemoryManager == this) {
        activeMemoryManager = 0;
    }
}

MemoryManager* MemoryManager::activeMemoryManager = 0;

void* MemoryManager::malloc(size_t size) {
    MemoryChunk* result = 0;
    for (MemoryChunk* chunk = first; chunk != 0 && result == 0;chunk = chunk->next)
        //如果找到的段大小大于要分配的大小并且这个段未被占用
        if (chunk->size > size && !chunk->allocated) result = chunk;
    //没找到
    if (result == 0) return 0;

    
     if (result->size >= size + sizeof(MemoryChunk) + 1) {
        //如果内存中有分配不均的一段
        //把多出的一段打包挂在result的下一个节点，并标记为已分配
        MemoryChunk* temp = (MemoryChunk*)((size_t)result + sizeof(MemoryChunk) + size);
        temp->allocated = false;
        temp->size = result->size - size - sizeof(MemoryChunk);
        temp->prev = result;
        temp->next = result->next;
        if (temp->next != 0) temp->next->prev = temp;

        result->size - size;
        result->next = temp;
    }
    result->allocated = true;
    return (void*)(((size_t)result) + sizeof(MemoryChunk));
}

void MemoryManager::free(void* ptr) {
    MemoryChunk* chunk = (MemoryChunk*)((size_t)ptr - sizeof(MemoryChunk));

    chunk->allocated = false;

    //与上一段合并
    if (chunk->prev != 0 && !chunk->prev->allocated) {
        chunk->prev->next = chunk->next;
        chunk->prev->size += chunk->size + sizeof(MemoryChunk);
        if (chunk->next != 0) chunk->next->prev = chunk->prev;

        chunk = chunk->prev;
    }

    //如果下一段没被使用且存在，把下一段并入
    if (chunk->next != 0 && !chunk->next->allocated) {
        chunk->size += chunk->next->size +sizeof(MemoryChunk);
        chunk->next = chunk->next->next;
        if (chunk->next != 0) chunk->next->prev = chunk;
    }


}

void* operator new(unsigned size) {
    if (gtos::MemoryManager::activeMemoryManager == 0) return 0;
    return gtos::MemoryManager::activeMemoryManager->malloc(size);
}

void* operator new[](unsigned size){
    if (gtos::MemoryManager::activeMemoryManager == 0) return 0;
    return gtos::MemoryManager::activeMemoryManager->malloc(size);
}

void* operator new(unsigned size, void* ptr) {
    return ptr;
}

void* operator new[](unsigned size, void* ptr){
    return ptr;
}


void operator delete(void* ptr) {
    if (gtos::MemoryManager::activeMemoryManager != 0) 
        gtos::MemoryManager::activeMemoryManager->free(ptr);
}

void operator delete[](void* ptr) {
    if (gtos::MemoryManager::activeMemoryManager != 0) 
        gtos::MemoryManager::activeMemoryManager->free(ptr);
}