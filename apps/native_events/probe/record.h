#ifndef NATIVE_EVENT_RECORD_H
#define NATIVE_EVENT_RECORD_H
#define EVENT_RECORD_VA 0x41000000U
struct EventRecord {
    unsigned version,mode,shared,foreign;
    volatile unsigned phase,failedLine,checks,progress;
    volatile unsigned workerStage[2];
    unsigned gate,automatic,ack[2],thread[2],array;
};
#endif
