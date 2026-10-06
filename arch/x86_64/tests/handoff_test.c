#include "handoff.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char b[BOOTINFO_LIMIT+8];
static void w32(unsigned off,uint32_t v) { for (unsigned i=0;i<4;++i)b[off+i]=(unsigned char)(v>>(8*i)); }
static void w64(unsigned off,uint64_t v) { w32(off,v);w32(off+4,v>>32); }
static unsigned make(void) {
    memset(b,0,sizeof(b));w32(0,64);w32(8,6);w32(12,40);w32(16,24);
    w64(24,0x100000);w64(32,0x3f00000);w32(40,1);
    w32(48,42);w32(52,8); /* Unknown tag safely ignored. */
    w32(56,0);w32(60,8);return 64;
}
static void reject(unsigned n) {
    struct handoff_summary s={123,456};
    assert(handoff_validate(b,n,0x100000,0x160000,&s));
    assert(s.usable_bytes==123 && s.mmap_entries==456);
}
int main(void) {
    struct handoff_summary s;
    make();assert(!handoff_validate(b,64,0x100000,0x160000,&s));
    assert(s.usable_bytes==0x3f00000 && s.mmap_entries==1);
    for (unsigned n=0;n<64;++n) { make();reject(n); }
    make();w32(0,15);reject(64);
    make();w32(0,65);reject(65);
    make();w32(0,BOOTINFO_LIMIT+8);reject(sizeof(b));
    make();w32(4,1);assert(!handoff_validate(b,64,0x100000,0x160000,&s));
    make();w32(12,0);reject(64);
    make();w32(12,7);reject(64);
    make();w32(12,UINT32_MAX);reject(64);
    make();w32(12,57);reject(64);
    make();w32(16,0);reject(64);
    make();w32(16,23);reject(64);
    make();w32(16,25);reject(64);
    make();w32(20,1);reject(64);
    make();w64(24,UINT64_MAX-1);w64(32,8);reject(64);
    make();w64(32,0);reject(64);
    make();w32(44,1);assert(!handoff_validate(b,64,0x100000,0x160000,&s));
    make();w32(40,2);reject(64);
    make();w64(24,0x120000);reject(64);
    make();w64(32,0x1000);reject(64);
    make();w32(8,42);reject(64);
    make();w32(48,6);reject(64);
    make();w32(48,0);reject(64);
    make();w32(56,42);reject(64);
    make();w32(60,16);reject(64);
    make();w32(52,9);reject(64);
    /* Larger entry_size is valid: only specified prefix is consumed. */
    make();w32(0,72);w32(12,48);w32(16,32);w32(48,0);w32(52,0);
    w32(56,42);w32(60,8);w32(64,0);w32(68,8);
    assert(!handoff_validate(b,72,0x100000,0x160000,&s));
    /* Overlap fails regardless of tag order/type; no hidden reserved override. */
    make();w32(0,88);w32(12,64);w64(48,0x110000);w64(56,4096);w32(64,2);w32(68,0);
    w32(72,42);w32(76,8);w32(80,0);w32(84,8);reject(88);
    /* Maximum admitted buffer with 2,729 whole, disjoint memory ranges. */
    memset(b,0,sizeof(b));w32(0,BOOTINFO_LIMIT);w32(8,6);w32(12,65512);w32(16,24);
    for (unsigned i=0;i<2729;++i) {
        unsigned off=24+i*24;
        w64(off,i ? 0x400000ull+(uint64_t)i*0x2000 : 0x100000);
        w64(off+8,i ? 4096 : 0x100000);w32(off+16,1);
    }
    w32(65520,42);w32(65524,8);w32(65528,0);w32(65532,8);
    assert(!handoff_validate(b,BOOTINFO_LIMIT,0x100000,0x160000,&s));
    assert(s.mmap_entries==2729);
    w32(65532,16);reject(BOOTINFO_LIMIT);
    w32(65532,8);w64(24+2728*24,0x100000);reject(BOOTINFO_LIMIT);
    /* Deterministic malformed-input stress under ASan/UBSan, bounded accessible size. */
    uint32_t random=0x47544f53;
    for (unsigned trial=0;trial<20000;++trial) {
        make(); random=random*1664525u+1013904223u;
        unsigned pos=random%64;random=random*1664525u+1013904223u;
        b[pos]^=(unsigned char)(random>>24);
        (void)handoff_validate(b,64,0x100000,0x160000,&s);
    }
    puts("x64 handoff host tests: PASS (bounds, tags, maps, 20,000 mutations)");
}
