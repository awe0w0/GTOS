#ifndef GTOS_THREAD_TLS_RECORD_H
#define GTOS_THREAD_TLS_RECORD_H
#define TLS_RECORD_VA 0x40040000U
#define TLS_WAVES 8U
struct TlsWorkerRecord {
    unsigned id, stackHandle, stackBase, stage, valueAddress, blockAddress;
    unsigned constructions, destructions, callbacks, order, rounds;
};
struct TlsRecord {
    unsigned version, mode, stage, error, checks, parent, go, wave;
    unsigned joins, emptyHeap;
    TlsWorkerRecord worker[4];
};
#endif
