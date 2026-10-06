#include "handoff.h"
static uint16_t u16(const unsigned char *p) {
    return (uint16_t)p[0] | (uint16_t)p[1]<<8;
}
static uint32_t u32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t u64(const unsigned char *p) { return u32(p) | (uint64_t)u32(p+4)<<32; }
static int overlap(uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    return a < d && c < b;
}
/* Called only after the current tag's fields and size have been validated. */
static struct handoff_range exclusion(const unsigned char *tag) {
    if (u32(tag)==3) return (struct handoff_range){u32(tag+8),u32(tag+12)};
    uint64_t start=u64(tag+8);
    return (struct handoff_range){start,start+(uint64_t)u32(tag+16)*u32(tag+24)};
}
static const char *framebuffer_validate(const unsigned char *tag, uint32_t size) {
    /* Wire layout follows GNU multiboot2.h: two reserved bytes at offset 30,
     * and a uint16_t palette count. Reserved fields must be ignored. */
    if (size < 32) return "framebuffer header";
    uint64_t start=u64(tag+8),pitch=u32(tag+16),width=u32(tag+20),height=u32(tag+24);
    unsigned bpp=tag[28],type=tag[29];
    if (!pitch || !width || !height || !bpp || bpp>64) return "framebuffer geometry";
    if (width > (UINT64_MAX-7)/bpp || (width*bpp+7)/8 > pitch)
        return "framebuffer pitch";
    if (height > UINT64_MAX/pitch || pitch*height > UINT64_MAX-start)
        return "framebuffer range";
    if (type==0) {
        if (size < 34 || bpp>8) return "framebuffer palette";
        uint32_t colors=u16(tag+32);
        if (!colors || colors > (1u<<bpp) || size != 34+colors*3)
            return "framebuffer palette";
    } else if (type==1) {
        if (size != 38) return "framebuffer RGB header";
        for (unsigned i=0;i<3;++i) {
            unsigned first=tag[32+i*2],bits=tag[33+i*2];
            if (!bits || first>=bpp || bits>bpp-first) return "framebuffer RGB mask";
            for (unsigned j=0;j<i;++j) {
                unsigned previous=tag[32+j*2],count=tag[33+j*2];
                if (first<previous+count && previous<first+bits)
                    return "framebuffer RGB overlap";
            }
        }
    } else if (type==2) {
        if (size != 32 || bpp!=16) return "framebuffer text";
    } else return "framebuffer type";
    return 0;
}
const char *handoff_parse(const void *bytes, size_t available,
                         uint64_t kernel_start, uint64_t kernel_end,
                         struct handoff_view *out) {
    const unsigned char *p=bytes;
    struct handoff_view result={0};
    if (!p || !out || available < 16) return "short header";
    uint32_t total=u32(p);
    if (total < 16 || total > BOOTINFO_LIMIT || total > available || (total & 7))
        return "total size";
    if (kernel_start >= kernel_end) return "kernel range";
    result.bytes=p;
    result.total_size=total;
    uint32_t off=8;
    int mmap_seen=0,kernel_usable=0;
    while (off < total) {
        if (total-off < 8) return "short tag";
        uint32_t type=u32(p+off),size=u32(p+off+4);
        if (size < 8 || size > total-off) return "tag size";
        uint32_t padded=(size+7u)&~7u; /* size <= 65536, cannot wrap */
        if (padded > total-off) return "tag padding";
        if (type==0) {
            if (size!=8 || off!=total-8) return "end tag";
            if (!mmap_seen || !kernel_usable) return "usable kernel missing";
            *out=result;
            return 0;
        }
        if (type==18) return "EFI boot services not terminated";
        if (type==6) {
            if (mmap_seen++ || size < 16) return "memory map header";
            uint32_t stride=u32(p+off+8);
            if (stride < 24 || (stride & 7) || u32(p+off+12) || (size-16)%stride)
                return "memory map stride";
            result.mmap_offset=off;
            result.mmap_stride=stride;
            for (uint32_t e=16;e<size;e+=stride) {
                const unsigned char *entry=p+off+e;
                uint64_t start=u64(entry),length=u64(entry+8);
                if (!length || length > UINT64_MAX-start) return "memory map range";
                uint64_t end=start+length;
                uint32_t kind=u32(entry+16);
                /* Any overlap is ambiguous, including two non-usable entries. */
                for (uint32_t prev=16;prev<e;prev+=stride) {
                    uint64_t a=u64(p+off+prev),b=a+u64(p+off+prev+8);
                    if (overlap(start,end,a,b)) return "overlapping memory map";
                }
                if (kind==1) {
                    if (length > UINT64_MAX-result.usable_bytes) return "memory size overflow";
                    result.usable_bytes+=length;
                    if (start<=kernel_start && end>=kernel_end) kernel_usable=1;
                }
                ++result.mmap_entries;
            }
        } else if (type==3 || type==8) {
            if (type==3) {
                if (size < 17 || u32(p+off+8)>=u32(p+off+12)) return "module range";
                uint32_t pos=16;
                while (pos<size && p[off+pos]) ++pos;
                if (pos==size) return "module string";
                ++result.module_count;
            } else {
                if (result.framebuffer_offset) return "duplicate framebuffer";
                const char *error=framebuffer_validate(p+off,size);
                if (error) return error;
                result.framebuffer_offset=off;
            }
            struct handoff_range current=exclusion(p+off);
            if (overlap(current.start,current.end,kernel_start,kernel_end))
                return "boot allocation overlaps kernel";
            for (uint32_t prev=8;prev<off;prev+=(u32(p+prev+4)+7u)&~7u) {
                uint32_t previous_type=u32(p+prev);
                if (previous_type==3 || previous_type==8) {
                    struct handoff_range previous=exclusion(p+prev);
                    if (overlap(current.start,current.end,previous.start,previous.end))
                        return "overlapping boot allocations";
                }
            }
        }
        off+=padded;
    }
    return "missing end tag";
}
const char *handoff_validate(const void *bytes, size_t available,
                            uint64_t kernel_start, uint64_t kernel_end,
                            struct handoff_summary *out) {
    if (!out) return "short header";
    struct handoff_view view;
    const char *error=handoff_parse(bytes,available,kernel_start,kernel_end,&view);
    if (error) return error;
    *out=(struct handoff_summary){view.usable_bytes,view.mmap_entries};
    return 0;
}
int handoff_memory_range(const struct handoff_view *view, uint32_t index,
                         struct handoff_memory_range *out) {
    if (!view || !out || index>=view->mmap_entries) return 0;
    const unsigned char *entry=view->bytes+view->mmap_offset+16+index*view->mmap_stride;
    uint64_t start=u64(entry);
    *out=(struct handoff_memory_range){start,start+u64(entry+8),u32(entry+16)};
    return 1;
}
int handoff_next_exclusion(const struct handoff_view *view, uint32_t *cursor,
                           struct handoff_range *out) {
    if (!view || !cursor || !out) return 0;
    uint32_t off=*cursor ? *cursor : 8;
    while (off<view->total_size-8) {
        const unsigned char *tag=view->bytes+off;
        uint32_t type=u32(tag);
        off+=(u32(tag+4)+7u)&~7u;
        if (type==3 || type==8) {
            *out=exclusion(tag);
            *cursor=off;
            return 1;
        }
    }
    *cursor=view->total_size;
    return 0;
}
