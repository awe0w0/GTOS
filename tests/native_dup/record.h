#ifndef GTOS_NATIVE_DUP_RECORD_H
#define GTOS_NATIVE_DUP_RECORD_H
struct CpuGuestRecord {
    unsigned version,mode,phase,checks,joined,workerStarted,workerDone;
    unsigned ids[3];
    unsigned fileShared,filePeer,filePhase,fileAlias,fileDirectory;
    unsigned fileTemporary[2],fileChecks,fileLeaks;
};
#define CPU_RECORD_VA 0x40020000U
#endif
