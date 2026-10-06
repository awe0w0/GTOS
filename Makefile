CXX ?= g++
AS ?= as
LD ?= ld
OBJCOPY ?= objcopy
PYTHON ?= python3
OPTIMIZATION ?= -O2
CXXFLAGS := $(OPTIMIZATION) -m32 -std=c++11 -Iinclude -ffreestanding -fno-use-cxa-atexit -nostdlib -fno-builtin -fno-rtti -fno-exceptions -fno-leading-underscore -fno-stack-protector -fno-pie -fno-asynchronous-unwind-tables -fcheck-new -Wno-write-strings -MMD -MP
ASFLAGS := --32
LDFLAGS := -melf_i386
CPP_SOURCES := $(shell find src -name '*.cpp' | sort)
ASM_SOURCES := $(shell find src -name '*.s' | sort)
OBJECTS := $(patsubst src/%.cpp,obj/%.o,$(CPP_SOURCES)) $(patsubst src/%.s,obj/%.o,$(ASM_SOURCES))
.PHONY: all clean run test
all: GTOS.iso
obj/%.o: src/%.cpp
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@
obj/%.o: src/%.s
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@
# Native probes are real freestanding i386 user binaries, never kernel entrypoints.
obj/native/%.o: apps/native/%.cpp apps/native/probe.h include/process/abi.h
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -mno-sse -mno-mmx -msoft-float -c $< -o $@
obj/native/peer-concurrent.o: apps/native/peer.cpp apps/native/probe.h include/process/abi.h
	mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -mno-sse -mno-mmx -msoft-float -DGTOS_NATIVE_PEER_TICKS=500 -c $< -o $@
obj/native/start.o: apps/native/start.s
	mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@
obj/native/%.elf: obj/native/%.o obj/native/start.o apps/native/image.ld
	$(LD) $(LDFLAGS) -T apps/native/image.ld -o $@ $< obj/native/start.o
.SECONDARY: obj/native/fault.o obj/native/peer.o obj/native/fault.elf obj/native/peer.elf obj/native/peer-concurrent.o obj/native/peer-concurrent.elf
obj/browser-probe/browser-probe.elf: apps/browser_probe/main.c apps/browser_probe/start.s apps/browser_probe/linker.ld tools/build-browser-probe.sh tools/browser_artifact.py
	./tools/build-browser-probe.sh "$(abspath obj/browser-probe)"
GTOS.bin: linker.ld $(OBJECTS)
	$(LD) $(LDFLAGS) -T $< -o $@ $(OBJECTS)
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
run: GTOS.iso
	./tools/run-qemu.sh
clean:
	rm -rf obj GTOS.bin GTOS.iso GTOS-legacy.iso GTOS-native-test.iso
test:
	./tests/run.sh
-include $(OBJECTS:.o=.d)

-include $(wildcard obj/native/*.d)
