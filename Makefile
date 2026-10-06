CXX ?= g++
AS ?= as
LD ?= ld
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
GTOS.bin: linker.ld $(OBJECTS)
	$(LD) $(LDFLAGS) -T $< -o $@ $(OBJECTS)
apps/catch.gtapp: apps/catch.json tools/package.py
	$(PYTHON) tools/package.py build apps/catch.json $@
GTOS.iso: GTOS.bin apps/catch.gtapp docs/desktop-font-license.txt Makefile
	mkdir -p obj/iso/boot/grub obj/iso/boot/licenses
	cp GTOS.bin obj/iso/boot/GTOS.bin
	cp apps/catch.gtapp obj/iso/boot/catch.gtapp
	cp docs/desktop-font-license.txt obj/iso/boot/licenses/
	printf 'set timeout=0\nset default=0\nmenuentry "GTOS Desktop" {\n multiboot /boot/GTOS.bin\n module /boot/catch.gtapp\n boot\n}\n' > obj/iso/boot/grub/grub.cfg
	grub-mkrescue --output=$@ obj/iso
GTOS-legacy.iso: GTOS.bin apps/catch.gtapp docs/desktop-font-license.txt Makefile
	mkdir -p obj/iso-legacy/boot/grub obj/iso-legacy/boot/licenses
	cp GTOS.bin obj/iso-legacy/boot/GTOS.bin
	cp apps/catch.gtapp obj/iso-legacy/boot/catch.gtapp
	cp docs/desktop-font-license.txt obj/iso-legacy/boot/licenses/
	printf 'set timeout=0\nset default=0\nmenuentry "GTOS Legacy VGA" {\n multiboot /boot/GTOS.bin legacy\n module /boot/catch.gtapp\n boot\n}\n' > obj/iso-legacy/boot/grub/grub.cfg
	grub-mkrescue --output=$@ obj/iso-legacy
run: GTOS.iso
	./tools/run-qemu.sh
clean:
	rm -rf obj GTOS.bin GTOS.iso GTOS-legacy.iso
test:
	./tests/run.sh
-include $(OBJECTS:.o=.d)
