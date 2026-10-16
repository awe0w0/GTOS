#ifndef GTOS_DIRECTORY_RUNTIME_RECORD_H
#define GTOS_DIRECTORY_RUNTIME_RECORD_H
#define DIRECTORY_RECORD_VA 0x40080000U
enum DirectoryPhase { DirectoryBoot=0, DirectoryFiles=1, DirectoryHeapFull=2,
    DirectoryHeapRecovered=3, DirectoryThreads=4, DirectoryRetained=5, DirectoryDone=6 };
struct DirectoryRecord {
    unsigned version,mode,phase,failedLine,checks,preinit,peerFile,mainId;
    unsigned fileChecks,threadChecks,heapBlocks,retained,heapBase,heapHandle;
    unsigned ids[3],workerStage[3],joined,tlsConstructed,tlsDestroyed;
    unsigned release0,release1,release2,workerDone,destructorDone,peerProgress;
    unsigned command,ack;
};
#endif
