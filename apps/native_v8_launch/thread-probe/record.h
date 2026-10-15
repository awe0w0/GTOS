#ifndef GTOS_THREAD_HEAP_RECORD_H
#define GTOS_THREAD_HEAP_RECORD_H
#define HEAP_RECORD_VA 0x40040000U
#define HEAP_ROUNDS 64U
struct HeapWorkerRecord { unsigned id, stackHandle, stackBase, stage, phase, rounds, moved, held; };
struct HeapRecord {
    unsigned version, mode, stage, error, checks, parent, go, heapBase;
    HeapWorkerRecord worker[4];
};
#endif
