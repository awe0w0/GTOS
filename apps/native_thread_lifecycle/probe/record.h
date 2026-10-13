#ifndef GTOS_THREAD_PROBE_RECORD_H
#define GTOS_THREAD_PROBE_RECORD_H
#define THREAD_RECORD_VA 0x40040000U
struct WorkerRecord {
    unsigned id, handle, base, stage, go, loops, initialMxcsr, finalMxcsr;
};
struct ThreadRecord {
    unsigned version, mode, stage, error, checks, parent, workerCount, file;
    unsigned loops, finalMxcsr, lastJoined, foreignId;
    WorkerRecord worker[4];
};
#endif
