#include <storage/littlefs_port.h>
extern "C" void gtos_lfs_assert_fail(const char* file,uint32_t line) {
    (void)file;(void)line;
    const char* text="GTOS LITTLEFS ASSERT FAIL\n";
    while (*text) asm volatile("outb %0,$0xe9" : : "a"(*text++));
    for (;;) asm volatile("cli; hlt");
}
extern "C" void* gtos_lfs_memcpy(void* to,const void* from,size_t size) {
    uint8_t* d=(uint8_t*)to;const uint8_t* s=(const uint8_t*)from;
    for (size_t i=0;i<size;i++) d[i]=s[i];
    return to;
}
extern "C" void* gtos_lfs_memset(void* to,int value,size_t size) {
    uint8_t* d=(uint8_t*)to;
    for (size_t i=0;i<size;i++) d[i]=(uint8_t)value;
    return to;
}
extern "C" int gtos_lfs_memcmp(const void* a,const void* b,size_t size) {
    const uint8_t* x=(const uint8_t*)a;const uint8_t* y=(const uint8_t*)b;
    for (size_t i=0;i<size;i++) if (x[i]!=y[i]) return (int)x[i]-(int)y[i];
    return 0;
}
extern "C" char* gtos_lfs_strchr(const char* text,int value) {
    const uint8_t c=(uint8_t)value;
    for (;;) { if ((uint8_t)*text==c) return (char*)text;if (!*text) return 0;text++; }
}
extern "C" char* gtos_lfs_strcpy(char* to,const char* from) {
    char* result=to;do { *to++=*from; } while (*from++);return result;
}
extern "C" size_t gtos_lfs_strspn(const char* text,const char* set) {
    size_t n=0;
    while (text[n]&&gtos_lfs_strchr(set,(uint8_t)text[n])) n++;
    return n;
}
extern "C" size_t gtos_lfs_strcspn(const char* text,const char* set) {
    size_t n=0;
    while (text[n]&&!gtos_lfs_strchr(set,(uint8_t)text[n])) n++;
    return n;
}
