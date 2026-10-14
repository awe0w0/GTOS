#ifndef GTOS_THREAD_TLS_RECORD_H
#define GTOS_THREAD_TLS_RECORD_H
#define TLS_RECORD_VA 0x40040000U
#define TLS_WAVES 8U
#define EXP2_BOUNDARY_CALLS_PER_CONTEXT (4U * 2U * 58U)
#define EXP2_ALL_CALLS_PER_CONTEXT (4U * (2U * 58U + 2098U))
struct TlsWorkerRecord {
    unsigned id, stackHandle, stackBase, stage, valueAddress, blockAddress;
    unsigned constructions, destructions, callbacks, order, rounds;
};
struct TlsRecord {
    unsigned version, mode, stage, error, checks, parent, go, wave;
    unsigned joins, emptyHeap;
    unsigned math_mode, math_seed, math_index, math_input_lo, math_input_hi;
    unsigned math_actual_lo, math_actual_hi, math_expected_lo, math_expected_hi;
    unsigned math_cw, math_sw, math_mx, math_errno, math_required, math_calls;
    TlsWorkerRecord worker[4];
};
#endif
