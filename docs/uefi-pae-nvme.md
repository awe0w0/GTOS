# UEFI, PAE and read-only storage

The Multiboot2 EFI profile is tested on QEMU 8.2.2 with Q35, x86-64 OVMF, 8 GiB
of RAM and eight virtual CPUs:

- Use `-m 8192 -smp 8,sockets=1,cores=8,threads=1` on
  `q35,max-ram-below-4g=1G`. The test puts the NVMe BAR and GOP framebuffer
  below 4 GiB with `-fw_cfg name=opt/ovmf/X-PciMmio64Mb,string=0`.
- The GPT disk uses a FAT32 EFI system partition and a read-only NTFS volume.
  Boot with `pae`, `pae-probe`, `nvme-probe`, `nvme-fs-probe` and
  `nvme-files` to exercise high frames, the NVMe controller and the Shell.
  `tcg-reset-mtrrs` is an explicit workaround for an AP reset state seen
  in QEMU TCG. Leave it off on other systems.
- PAE keeps the virtual address space 32-bit. The kernel can map and reclaim
  physical frames at and above 4 GiB; anonymous private pages can use those
  frames across native process switches. The boot check verifies two distinct
  frames, writes from CPL3 and checks the free high-frame count after Reap.
- The desktop uses the 1920×1080 GOP framebuffer. The system checks the PAE
  paging root of each AP and verifies work from all seven APs; the BSP still
  runs the general scheduler.

Open **Shell** from the desktop launcher. `partitions` lists GPT partitions;
`mount 2` mounts the NTFS volume on the test disk. Use `ls`, `cd`, `pwd` and
`cat` to browse. This path performs read-only file operations and refuses
unsupported entries. QEMU tests attach a fixture disk with `readonly=on`.
They do not pass through or modify a host physical disk.

The NVMe driver uses up to four outstanding reads, depending on the controller
queue size and available low-memory DMA pages. It matches completions by
command ID and resets the controller before releasing the pages. The optional
`nvme-read-bench` boot check needs a dedicated partition named
`GTOS NVMe test data`. It reads a 512 MiB fixture and checks the first and
last marker of every MiB. This measures the native buffered read path in that
VM; physical NVMe performance, GPU acceleration and general SMP scheduling
remain separate from this validation.
