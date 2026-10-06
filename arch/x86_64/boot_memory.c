#include "boot_memory.h"
/* Fixed 2 KiB workspace, independent of tag count or selected pool size. */
#define WINDOW_PAGES (BOOT_MEMORY_CEILING/BOOT_MEMORY_PAGE_SIZE)
#define BITMAP_BYTES (WINDOW_PAGES/8)
static int overlap(uint64_t a,uint64_t b,uint64_t c,uint64_t d) {
    return a<d && c<b;
}
static void mark(unsigned char *bitmap,uint64_t start,uint64_t end,int available) {
    if (start<BOOT_MEMORY_FLOOR) start=BOOT_MEMORY_FLOOR;
    if (end>BOOT_MEMORY_CEILING) end=BOOT_MEMORY_CEILING;
    if (start>=end) return;
    /* Clip before rounding: these additions cannot escape the 64 MiB window.
     * Availability rounds inward; exclusions round outward. */
    uint32_t first=(uint32_t)((start+(available ? BOOT_MEMORY_PAGE_SIZE-1 : 0))/BOOT_MEMORY_PAGE_SIZE);
    uint32_t limit=(uint32_t)((end+(available ? 0 : BOOT_MEMORY_PAGE_SIZE-1))/BOOT_MEMORY_PAGE_SIZE);
    for (uint32_t page=first;page<limit;++page) {
        unsigned char bit=(unsigned char)(1u<<(page&7));
        if (available) bitmap[page/8]|=bit;
        else bitmap[page/8]&=(unsigned char)~bit;
    }
}
const char *boot_memory_select(const void *private_copy,size_t available,
                              const struct boot_memory_request *request,
                              struct boot_memory_selection *out) {
    if (!request || !out) return "pool arguments";
    uint32_t minimum=request->min_frames ? request->min_frames : BOOT_MEMORY_MIN_FRAMES;
    uint32_t maximum=request->max_frames ? request->max_frames : BOOT_MEMORY_MAX_FRAMES;
    if (minimum>maximum || maximum>BOOT_MEMORY_MAX_FRAMES) return "pool limits";
    if (request->retained_count>BOOT_MEMORY_MAX_RETAINED ||
        (request->retained_count && !request->retained)) return "retained ranges";
    for (size_t i=0;i<request->retained_count;++i)
        if (request->retained[i].start>=request->retained[i].end) return "retained range";
    if (request->kernel_start<BOOT_MEMORY_FLOOR ||
        request->kernel_end>BOOT_MEMORY_CEILING || request->kernel_start>=request->kernel_end)
        return "pool kernel range";
    struct handoff_view view;
    const char *error=handoff_parse(private_copy,available,request->kernel_start,request->kernel_end,&view);
    if (error) return error;
    if (request->original_info_size!=view.total_size ||
        request->original_info_start<BOOT_MEMORY_PAGE_SIZE ||
        (request->original_info_start&7) ||
        request->original_info_start>=BOOT_MEMORY_CEILING ||
        request->original_info_size>BOOT_MEMORY_CEILING-request->original_info_start)
        return "original information range";
    uint64_t info_end=request->original_info_start+request->original_info_size;
    if (overlap(request->kernel_start,request->kernel_end,request->original_info_start,info_end))
        return "information overlaps kernel";
    struct handoff_range excluded;
    uint32_t cursor=0;
    while (handoff_next_exclusion(&view,&cursor,&excluded))
        if (overlap(excluded.start,excluded.end,request->original_info_start,info_end))
            return "boot allocation overlaps information";

    unsigned char bitmap[BITMAP_BYTES]={0};
    struct handoff_memory_range range;
    for (uint32_t i=0;handoff_memory_range(&view,i,&range);++i)
        if (range.type==1) mark(bitmap,range.start,range.end,1);
    mark(bitmap,request->kernel_start,request->kernel_end,0);
    mark(bitmap,request->original_info_start,info_end,0);
    cursor=0;
    while (handoff_next_exclusion(&view,&cursor,&excluded)) mark(bitmap,excluded.start,excluded.end,0);
    for (size_t i=0;i<request->retained_count;++i)
        mark(bitmap,request->retained[i].start,request->retained[i].end,0);
    uint32_t eligible=0;
    for (uint32_t page=BOOT_MEMORY_FLOOR/BOOT_MEMORY_PAGE_SIZE;page<WINDOW_PAGES;++page)
        if (bitmap[page/8]&(1u<<(page&7))) ++eligible;
    if (eligible<minimum) return "insufficient eligible frames";
    uint32_t managed=eligible<maximum ? eligible : maximum;
    /* All potentially failing work has completed. Publish only immutable facts;
     * even unused array slots are deterministic, with no stack-sized copy. */
    out->firmware_usable_bytes=view.usable_bytes;
    out->mmap_entries=view.mmap_entries;
    out->eligible_count=eligible;
    out->managed_count=managed;
    uint32_t count=0;
    for (uint32_t page=BOOT_MEMORY_FLOOR/BOOT_MEMORY_PAGE_SIZE;page<WINDOW_PAGES && count<managed;++page)
        if (bitmap[page/8]&(1u<<(page&7))) out->frames[count++]=(uint64_t)page*BOOT_MEMORY_PAGE_SIZE;
    for (;count<BOOT_MEMORY_MAX_FRAMES;++count) out->frames[count]=0;
    return 0;
}
