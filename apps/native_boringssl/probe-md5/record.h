#ifndef GTOS_NATIVE_CRYPTO_MD5_RECORD_H
#define GTOS_NATIVE_CRYPTO_MD5_RECORD_H
#define CRYPTO_RECORD_VA 0x4005b000U
struct CryptoMd5Record {
    unsigned version, mode, stage, error, checks, calls, oneShot, incremental;
    unsigned parent, inputEnd, outputEnd, expectedFault, faultError;
    unsigned row, stride, alignment, actual, expected;
};
#endif
