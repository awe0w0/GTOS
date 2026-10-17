#ifndef GTOS_NATIVE_CRYPTO_THREAD_PROBE_API_H
#define GTOS_NATIVE_CRYPTO_THREAD_PROBE_API_H
struct CryptoLockSnapshot {
    unsigned next, serving, writer, readers, ownDepth;
};
extern "C" {
unsigned crypto_probe_current_tid();
void crypto_probe_yield();
void crypto_probe_lock(unsigned action);
void crypto_probe_snapshot(CryptoLockSnapshot* output);
void crypto_probe_seed(unsigned value);
void crypto_probe_once(void (*initialize)());
unsigned crypto_probe_nested_once();
void crypto_probe_bad(unsigned mode);
void* crypto_probe_get(unsigned index);
unsigned crypto_probe_set(unsigned index,void* value,void (*destroy)(void*));
}
#endif
