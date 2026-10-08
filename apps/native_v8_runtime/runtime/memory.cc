// Complete scalar compiler byte helpers; no host runtime or allocator.
extern "C" void* memcpy(void* out, const void* in, unsigned bytes) {
    volatile unsigned char* d = (volatile unsigned char*)out;
    const volatile unsigned char* s = (const volatile unsigned char*)in;
    for (unsigned i = 0; i < bytes; ++i) d[i] = s[i];
    return out;
}
extern "C" void* memset(void* out, int value, unsigned bytes) {
    volatile unsigned char* d = (volatile unsigned char*)out;
    for (unsigned i = 0; i < bytes; ++i) d[i] = (unsigned char)value;
    return out;
}
