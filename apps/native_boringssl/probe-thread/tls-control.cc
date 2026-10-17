extern "C" void* __real_crypto_probe_get(unsigned);
namespace {
void* shared[5];
}
extern "C" void* __wrap_crypto_probe_get(unsigned index) {
    void* current=__real_crypto_probe_get(index);
    if (current) __atomic_store_n(&shared[index],current,__ATOMIC_RELEASE);
    return __atomic_load_n(&shared[index],__ATOMIC_ACQUIRE);
}
