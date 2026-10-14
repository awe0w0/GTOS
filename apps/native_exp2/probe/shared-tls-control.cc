// Test-only cache-alias control. The first address comes from the real provider;
// subsequent threads deliberately receive that same address.
extern "C" void* __real___emutls_get_address(void*);
extern "C" void* __wrap___emutls_get_address(void* control) {
    static void* keys[64];
    static void* addresses[64];
    for (unsigned i=0;i<64;++i) {
        if (keys[i]==control) return addresses[i];
        if (keys[i]==nullptr) {
            void* result=__real___emutls_get_address(control);
            addresses[i]=result; keys[i]=control; return result;
        }
    }
    __builtin_trap();
}
