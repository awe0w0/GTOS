#ifndef GTOS_NATIVE_CRYPTO_THREAD_RECORD_H
#define GTOS_NATIVE_CRYPTO_THREAD_RECORD_H
#define CRYPTO_THREAD_RECORD_VA 0x4005b000U
#define CRYPTO_THREAD_WAVES 8U
struct CryptoThreadWorker {
    unsigned id, stage, release, recurse, recursed, order;
    unsigned destroyed, nullDestroyed, reentered, tlsRounds;
};
struct CryptoThreadRecord {
    unsigned version, mode, stage, error, checks, parent, scenario, wave;
    unsigned joins, emptyHeap, sequence, lockScenarios;
    unsigned onceEntries, onceRelease, onceFirst, onceSecond, onceCalls;
    unsigned failedRegistrations, failedDestructors, successfulRetry;
    unsigned go, snapshotNext, snapshotServing, snapshotReaders;
    CryptoThreadWorker worker[4];
};
#endif
