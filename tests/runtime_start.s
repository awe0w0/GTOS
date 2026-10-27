# Minimal freestanding entry for the NativeRuntime host unit.
#
# A static i386 ELF with no libc supplies its own process entry. At _start the kernel has already placed argc at (%esp) and
# the argv vector just above it, so this entry forwards both: one ELF can then run one case per process, which is what the
# runtime needs because a successful Activate publishes a process-wide owner that has no destroy path.
#
# runtimeTestMain returns the number of failed expectations, and that becomes the process exit status.
#
# Only this host unit uses this entry. The kernel image never links it.

.section .text
.global _start
.extern runtimeTestMain
_start:
    # ---- map the window the allocator seeds
    #
    # The production paging object writes its page directory and its tables through the physical addresses the allocator
    # hands it: `((uint32_t*)directory)[di] = frame | Present | Writable`. On the machine those addresses are memory. In a
    # host process they are memory only if something mapped them, and this unit's frames begin at 0x00800000, which a static
    # binary loaded at 0x08048000 does not own.
    #
    # Without this the first paging case faults on the very first write to the directory, and the fault arrives from inside
    # production code that is behaving correctly for the machine it was written for. A fault there is indistinguishable from
    # a real defect in the code under test, so the window is mapped before any case runs and the mapping is checked.
    #
    # The length is the four megabytes the seed makes free, which is exactly the range frames 0x800..0xBFF cover.
    mov $192, %eax          # mmap2
    mov $0x00800000, %ebx   # addr: the first frame the allocator seeds
    mov $0x00400000, %ecx   # length: four megabytes
    mov $7, %edx            # PROT_READ | PROT_WRITE | PROT_EXEC
    mov $0x32, %esi         # MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS
    mov $-1, %edi           # fd: ignored for an anonymous mapping
    xor %ebp, %ebp          # offset, in pages
    int $0x80
    cmp $0x00800000, %eax   # the arena has to be where the allocator expects it
    je 1f
    # The arena could not be mapped, so no case can run. Exiting with a distinct status is the only honest outcome: running
    # anyway would fault inside the code under test and report a defect that is not there.
    mov $70, %ebx           # EX_SOFTWARE
    mov $1, %eax            # exit
    int $0x80
1:
    mov (%esp), %eax        # argc
    lea 4(%esp), %ebx       # argv
    push %ebx
    push %eax
    call runtimeTestMain
    add $8, %esp
    mov %eax, %ebx
    mov $1, %eax
    int $0x80
    hlt
