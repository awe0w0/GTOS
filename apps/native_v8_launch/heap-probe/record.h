#ifndef GTOS_LARGE_HEAP_RECORD_H
#define GTOS_LARGE_HEAP_RECORD_H
#define HEAP_RECORD_VA 0x40040000U
struct HeapRecord { unsigned version, mode, stage, error, checks, held; };
#endif
