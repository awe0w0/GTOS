# Native PNG desktop consumer

A real freestanding i386 CPL3 application retrieves the versioned immutable
1108-byte PNG through bounded INFO/READ calls and decodes it with the previously
qualified Wuffs/Skia objects. Both original full BGRA and premultiplied RGBA
oracles are checked before the complete 16x16 raster is published through the
new surface v1 BEGIN/WRITE/PRESENT calls.

The same process exercises bad wire sizes, unmapped/overflowing sources,
read-only and crossing-page requests, stale handles, failed complete-chunk
premultiplication, partial PRESENT and ABORT. After publication it poisons its
own storage and exits while owning another incomplete draft. The guest checks
process-page reclamation and new-transaction admission; independent framebuffer
pixel checks cover the displayed snapshot and window close/reopen.

Build with `tools/build-png-resource-probe.py --surface` and the existing
qualified archive/cache/toolchain arguments. Original resource and codec probes
remain separate. This program is an independent native software-output module
for the 32-bit Chromium port.
