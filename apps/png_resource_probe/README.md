# Native immutable PNG resource consumer

This independent ABI1 application retrieves public resource ID 1 through the
versioned INFO/READ extension, then uses the unchanged qualified Wuffs PNG and
Skia integer component. It embeds independent BGRA and premultiplied RGBA pixel
oracles and a full-resource CRC, but no PNG input bytes. The kernel owns the
1108-byte input in immutable rodata.

`tools/generate-native-png-resource.py` reproducibly generates the 16x16 RGBA8
application icon, its explicit source pixels, exact stored-DEFLATE PNG, kernel
include, pixel oracles, and provenance manifest. The nearest-integer oracle uses
`(channel * alpha + 127) // 255`, separately from the Skia implementation. These
unsigned hashes prove reproducibility; runtime identity comes from the fixed
compiled resource catalog.

The app checks descriptor snapshot semantics, syscall register/segment
preservation, byte limits, versions, IDs, EOF, overflow, mapped page crossings,
readonly/unmapped/kernel/null destinations, and unchanged writable prefixes on
rejected page crossings. Its final console marker confirms resource consumption
and exact decoded pixels. It provides no native PNG presentation API.

The original 46-fixture PNG application and qualifier are retained unchanged.
File/page/stack limits remain 65536 bytes, 256 mapped pages, and 8176 usable user
stack bytes. A build pass does not establish the complete stack bound or guest
acceptance; those require independent analysis of the exact ELF and a guest run.
