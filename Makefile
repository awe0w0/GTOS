CXX ?= g++
AS ?= as
LD ?= ld
OBJCOPY ?= objcopy
PYTHON ?= python3
OPTIMIZATION ?= -O2
BASE_CXXFLAGS := $(OPTIMIZATION) -m32 -std=c++11 -Iinclude -ffreestanding -fno-use-cxa-atexit -nostdlib -fno-builtin -fno-rtti -fno-exceptions -fno-leading-underscore -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -fcheck-new -Wno-write-strings -MMD -MP
CXXFLAGS ?= $(BASE_CXXFLAGS)
# Keep the mandatory kernel policy last, even when callers override CXXFLAGS.
override KERNEL_INTEGER_FLAGS := $(shell cat tools/kernel-cxxflags)
# User probes have a separate policy; enabled FP probes may replace these flags.
NATIVE_CXXFLAGS ?= $(BASE_CXXFLAGS) -mno-sse -mno-mmx -msoft-float -mno-avx -mno-avx2 -mno-avx512f -mno-xsave
KERNEL_OBJDIR ?= obj
KERNEL_BINARY ?= GTOS.bin
ASFLAGS := --32
LDFLAGS := -melf_i386
CPP_SOURCES := $(shell find src -name '*.cpp' | sort)
ASM_SOURCES := $(shell find src -name '*.s' | sort)
OBJECTS := $(patsubst src/%.cpp,$(KERNEL_OBJDIR)/%.o,$(CPP_SOURCES)) $(patsubst src/%.s,$(KERNEL_OBJDIR)/%.asm.o,$(ASM_SOURCES))
# A failed post-link audit must not leave a timestamp-current runnable target.
.DELETE_ON_ERROR:
.PHONY: all clean run test test-kernel-integer
all: GTOS.iso
$(KERNEL_OBJDIR)/%.o: src/%.cpp tools/kernel-cxxflags Makefile
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(KERNEL_INTEGER_FLAGS) -c $< -o $@
$(KERNEL_OBJDIR)/%.asm.o: src/%.s
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@
# Native probes are real freestanding i386 user binaries, never kernel entrypoints.
obj/native/%.o: apps/native/%.cpp apps/native/probe.h include/process/abi.h
	mkdir -p $(@D)
	$(CXX) $(NATIVE_CXXFLAGS) -c $< -o $@
obj/native/peer-concurrent.o: apps/native/peer.cpp apps/native/probe.h include/process/abi.h
	mkdir -p $(@D)
	$(CXX) $(NATIVE_CXXFLAGS) -DGTOS_NATIVE_PEER_TICKS=500 -c $< -o $@
obj/native/start.o: apps/native/start.s
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@
obj/native/%.elf: obj/native/%.o obj/native/start.o apps/native/image.ld
	$(LD) $(LDFLAGS) -T apps/native/image.ld -o $@ $< obj/native/start.o
.SECONDARY: obj/native/fault.o obj/native/peer.o obj/native/fault.elf obj/native/peer.elf obj/native/peer-concurrent.o obj/native/peer-concurrent.elf
obj/browser-probe/browser-probe.elf: apps/browser_probe/main.c apps/browser_probe/start.s apps/browser_probe/linker.ld tools/build-browser-probe.sh tools/browser_artifact.py
	./tools/build-browser-probe.sh "$(abspath obj/browser-probe)"
# Explicit enabled-profile acceptance payloads. Their FP instructions live in
# user assembly; this does not relax the kernel compiler/audit policy.
obj/native-fp-desktop/fault.o: tests/native_fp_desktop_user.cpp apps/native/probe.h include/process/abi.h Makefile
	mkdir -p $(@D)
	$(CXX) $(NATIVE_CXXFLAGS) -DGTOS_FP_DESKTOP_FAULT=1 -c $< -o $@
obj/native-fp-desktop/peer.o: tests/native_fp_desktop_user.cpp apps/native/probe.h include/process/abi.h Makefile
	mkdir -p $(@D)
	$(CXX) $(NATIVE_CXXFLAGS) -DGTOS_FP_DESKTOP_FAULT=0 -c $< -o $@
obj/native-fp-desktop/helper.o: tests/native_fp_desktop_user.s
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@
obj/native-fp-desktop/%.elf: obj/native-fp-desktop/%.o obj/native-fp-desktop/helper.o obj/native/start.o apps/native/image.ld tools/audit-kernel-instructions.py
	$(LD) $(LDFLAGS) -T apps/native/image.ld -Map $@.map -o $@ $< obj/native-fp-desktop/helper.o obj/native/start.o
	$(PYTHON) tools/audit-kernel-instructions.py --map $@.map --allow-user-symbol native_fp_desktop_seed --allow-user-symbol native_fp_desktop_capture $@
# QEMU diagnostic payloads bypass only the separately qualified pointer gap.
# Separate paths prevent either these ELFs or the test CPL0 probe entering a
# production/strict image through stale object reuse.
FP_DIAGNOSTIC_OBJDIR := $(KERNEL_OBJDIR)/native-fp-desktop-diagnostic
FP_DIAGNOSTIC_BINARY ?= GTOS-native-fp-diagnostic.bin
FP_DIAGNOSTIC_ISO ?= GTOS-native-fp-diagnostic.iso
FP_DIAGNOSTIC_OBJECTS := $(filter-out $(KERNEL_OBJDIR)/kernel.o,$(OBJECTS)) $(FP_DIAGNOSTIC_OBJDIR)/kernel.o $(FP_DIAGNOSTIC_OBJDIR)/qualifier.o $(FP_DIAGNOSTIC_OBJDIR)/qualifier-asm.o
$(FP_DIAGNOSTIC_OBJDIR)/kernel.o: src/kernel.cpp tools/kernel-cxxflags Makefile
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(KERNEL_INTEGER_FLAGS) -DGTOS_FP_DESKTOP_DIAGNOSTIC=1 -c $< -o $@
$(FP_DIAGNOSTIC_OBJDIR)/qualifier.o: tests/native_fp_desktop_diagnostic.cpp tools/kernel-cxxflags Makefile
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(KERNEL_INTEGER_FLAGS) -c $< -o $@
$(FP_DIAGNOSTIC_OBJDIR)/qualifier-asm.o: tests/native_fp_desktop_diagnostic.s
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@
$(FP_DIAGNOSTIC_OBJDIR)/fault.o: tests/native_fp_desktop_user.cpp apps/native/probe.h include/process/abi.h Makefile
	mkdir -p $(@D)
	$(CXX) $(NATIVE_CXXFLAGS) -DGTOS_FP_DESKTOP_FAULT=1 -DGTOS_FP_DESKTOP_POINTER_DIAGNOSTIC=1 -c $< -o $@
$(FP_DIAGNOSTIC_OBJDIR)/peer.o: tests/native_fp_desktop_user.cpp apps/native/probe.h include/process/abi.h Makefile
	mkdir -p $(@D)
	$(CXX) $(NATIVE_CXXFLAGS) -DGTOS_FP_DESKTOP_FAULT=0 -DGTOS_FP_DESKTOP_POINTER_DIAGNOSTIC=1 -c $< -o $@
$(FP_DIAGNOSTIC_OBJDIR)/%.elf: $(FP_DIAGNOSTIC_OBJDIR)/%.o obj/native-fp-desktop/helper.o obj/native/start.o apps/native/image.ld tools/audit-kernel-instructions.py
	$(LD) $(LDFLAGS) -T apps/native/image.ld -Map $@.map -o $@ $< obj/native-fp-desktop/helper.o obj/native/start.o
	$(PYTHON) tools/audit-kernel-instructions.py --map $@.map --allow-user-symbol native_fp_desktop_seed --allow-user-symbol native_fp_desktop_capture $@
$(FP_DIAGNOSTIC_BINARY): linker.ld $(FP_DIAGNOSTIC_OBJECTS) tools/audit-kernel-instructions.py
	$(LD) $(LDFLAGS) -T $< -Map $(FP_DIAGNOSTIC_OBJDIR)/kernel.map -o $@ $(FP_DIAGNOSTIC_OBJECTS)
	$(PYTHON) tools/audit-kernel-instructions.py --map $(FP_DIAGNOSTIC_OBJDIR)/kernel.map --source-root . --allow-test-kernel-symbol native_fp_desktop_pointer_probe_asm $@
$(KERNEL_BINARY): linker.ld $(OBJECTS) tools/audit-kernel-instructions.py
	$(LD) $(LDFLAGS) -T $< -Map $(KERNEL_OBJDIR)/kernel.map -o $@ $(OBJECTS)
	$(PYTHON) tools/audit-kernel-instructions.py --map $(KERNEL_OBJDIR)/kernel.map --source-root . $@
apps/catch.gtapp: apps/catch.json tools/package.py
	$(PYTHON) tools/package.py build apps/catch.json $@
GTOS.iso: GTOS.bin apps/catch.gtapp obj/native/fault.elf obj/native/peer.elf obj/browser-probe/browser-probe.elf docs/desktop-font-license.txt apps/fonts/OFL.txt apps/fonts/README.md apps/fonts/han_manifest.json Makefile
	mkdir -p obj/iso/boot/grub obj/iso/boot/licenses
	cp GTOS.bin obj/iso/boot/GTOS.bin
	cp apps/catch.gtapp obj/iso/boot/catch.gtapp
	cp obj/native/fault.elf obj/iso/boot/native-fault.elf
	cp obj/native/peer.elf obj/iso/boot/native-peer.elf
	cp obj/browser-probe/browser-probe.elf obj/iso/boot/browser-probe.elf
	cp docs/desktop-font-license.txt obj/iso/boot/licenses/
	cp apps/fonts/OFL.txt obj/iso/boot/licenses/Noto-OFL.txt
	cp apps/fonts/README.md obj/iso/boot/licenses/noto-font-provenance.md
	cp apps/fonts/han_manifest.json obj/iso/boot/licenses/
	printf 'set timeout=0\nset default=0\nmenuentry "GTOS Desktop" {\n multiboot /boot/GTOS.bin\n module /boot/catch.gtapp\n module /boot/native-fault.elf\n module /boot/native-peer.elf\n module /boot/browser-probe.elf\n boot\n}\n' > obj/iso/boot/grub/grub.cfg
	grub-mkrescue --output=$@ obj/iso
GTOS-legacy.iso: GTOS.bin apps/catch.gtapp obj/native/fault.elf obj/native/peer.elf obj/browser-probe/browser-probe.elf docs/desktop-font-license.txt apps/fonts/OFL.txt apps/fonts/README.md apps/fonts/han_manifest.json Makefile
	mkdir -p obj/iso-legacy/boot/grub obj/iso-legacy/boot/licenses
	cp GTOS.bin obj/iso-legacy/boot/GTOS.bin
	cp apps/catch.gtapp obj/iso-legacy/boot/catch.gtapp
	cp obj/native/fault.elf obj/iso-legacy/boot/native-fault.elf
	cp obj/native/peer.elf obj/iso-legacy/boot/native-peer.elf
	cp obj/browser-probe/browser-probe.elf obj/iso-legacy/boot/browser-probe.elf
	cp docs/desktop-font-license.txt obj/iso-legacy/boot/licenses/
	cp apps/fonts/OFL.txt obj/iso-legacy/boot/licenses/Noto-OFL.txt
	cp apps/fonts/README.md obj/iso-legacy/boot/licenses/noto-font-provenance.md
	cp apps/fonts/han_manifest.json obj/iso-legacy/boot/licenses/
	printf 'set timeout=0\nset default=0\nmenuentry "GTOS Legacy VGA" {\n multiboot /boot/GTOS.bin legacy\n module /boot/catch.gtapp\n module /boot/native-fault.elf\n module /boot/native-peer.elf\n module /boot/browser-probe.elf\n boot\n}\n' > obj/iso-legacy/boot/grub/grub.cfg
	grub-mkrescue --output=$@ obj/iso-legacy
# Slow peer only for concurrent UI/IRQ acceptance; normal boot remains fast.
GTOS-native-test.iso: GTOS.iso obj/native/peer-concurrent.elf
	mkdir -p obj/iso-native-test
	cp -a obj/iso/. obj/iso-native-test/
	cp obj/native/peer-concurrent.elf obj/iso-native-test/boot/native-peer.elf
	grub-mkrescue --output=$@ obj/iso-native-test
# The normal desktop and disabled-profile acceptance keep their original policy.
GTOS-native-fp-test.iso: GTOS.iso obj/native-fp-desktop/fault.elf obj/native-fp-desktop/peer.elf Makefile
	mkdir -p obj/iso-native-fp-test
	cp -a obj/iso/. obj/iso-native-fp-test/
	cp obj/native-fp-desktop/fault.elf obj/iso-native-fp-test/boot/native-fault.elf
	cp obj/native-fp-desktop/peer.elf obj/iso-native-fp-test/boot/native-peer.elf
	printf 'set timeout=0\nset default=0\nmenuentry "GTOS Native FP Acceptance" {\n set gfxpayload=800x600x32\n multiboot /boot/GTOS.bin native-fp-test\n module /boot/catch.gtapp\n module /boot/native-fault.elf\n module /boot/native-peer.elf\n module /boot/browser-probe.elf\n boot\n}\n' > obj/iso-native-fp-test/boot/grub/grub.cfg
	grub-mkrescue --output=$@ obj/iso-native-fp-test
# Supplementary diagnostic only, never the strict ownership acceptance ISO.
$(FP_DIAGNOSTIC_ISO): $(FP_DIAGNOSTIC_BINARY) apps/catch.gtapp $(FP_DIAGNOSTIC_OBJDIR)/fault.elf $(FP_DIAGNOSTIC_OBJDIR)/peer.elf obj/browser-probe/browser-probe.elf Makefile
	mkdir -p $(FP_DIAGNOSTIC_OBJDIR)/iso/boot/grub
	cp $(FP_DIAGNOSTIC_BINARY) $(FP_DIAGNOSTIC_OBJDIR)/iso/boot/GTOS.bin
	cp apps/catch.gtapp $(FP_DIAGNOSTIC_OBJDIR)/iso/boot/catch.gtapp
	cp $(FP_DIAGNOSTIC_OBJDIR)/fault.elf $(FP_DIAGNOSTIC_OBJDIR)/iso/boot/native-fault.elf
	cp $(FP_DIAGNOSTIC_OBJDIR)/peer.elf $(FP_DIAGNOSTIC_OBJDIR)/iso/boot/native-peer.elf
	cp obj/browser-probe/browser-probe.elf $(FP_DIAGNOSTIC_OBJDIR)/iso/boot/browser-probe.elf
	printf 'set timeout=0\nset default=0\nmenuentry "GTOS FP Desktop DIAGNOSTIC ONLY" {\n set gfxpayload=800x600x32\n multiboot /boot/GTOS.bin native-fp-test native-fp-diagnostic\n module /boot/catch.gtapp\n module /boot/native-fault.elf\n module /boot/native-peer.elf\n module /boot/browser-probe.elf\n boot\n}\n' > $(FP_DIAGNOSTIC_OBJDIR)/iso/boot/grub/grub.cfg
	grub-mkrescue --output=$@ $(FP_DIAGNOSTIC_OBJDIR)/iso
run: GTOS.iso
	./tools/run-qemu.sh
clean:
	rm -rf obj GTOS.bin GTOS.iso GTOS-legacy.iso GTOS-native-test.iso GTOS-native-fp-test.iso GTOS-native-fp-diagnostic.bin GTOS-native-fp-diagnostic.iso
test:
	./tests/run.sh
test-kernel-integer:
	./tests/kernel_integer_test.sh
-include $(OBJECTS:.o=.d)

-include $(wildcard obj/native/*.d)
-include $(wildcard obj/native-fp-desktop/*.d)

-include $(wildcard $(FP_DIAGNOSTIC_OBJDIR)/*.d)
