extern "C" void __wrap_crypto_probe_once(void (*initialize)()) {
    initialize();
}
