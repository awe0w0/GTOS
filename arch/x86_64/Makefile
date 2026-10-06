# Deliberately separate: invoke from the repository root with -f.
CC := gcc
LD := ld
PYTHON ?= python3
GRUB_MKRESCUE ?= grub-mkrescue
BUILD ?= obj/x86_64
OPT ?= -O2
TEST_INJECT ?= 0
CFLAGS := $(OPT) -std=c11 -Wall -Wextra -Werror -ffreestanding -fno-builtin -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -fno-unwind-tables -m64 -mno-red-zone -mgeneral-regs-only -DTEST_INJECT=$(TEST_INJECT) -Iarch/x86_64
OBJECTS := $(BUILD)/boot.o $(BUILD)/traps.o $(BUILD)/kernel.o $(BUILD)/handoff.o
.PHONY: all iso test test-host clean FORCE
all: iso
$(BUILD):
	mkdir -p $@
$(BUILD)/config: FORCE | $(BUILD)
	@printf '%s\n' '$(CC) $(CFLAGS)' > $@.tmp; cmp -s $@.tmp $@ || cp $@.tmp $@; rm $@.tmp
$(BUILD)/%.o: arch/x86_64/%.S $(BUILD)/config | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/%.o: arch/x86_64/%.c arch/x86_64/handoff.h $(BUILD)/config | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@
$(BUILD)/kernel.elf: $(OBJECTS) arch/x86_64/linker.ld
	$(LD) -m elf_x86_64 -nostdlib --fatal-warnings -z max-page-size=0x1000 -T arch/x86_64/linker.ld -o $@ $(OBJECTS)
iso: $(BUILD)/GTOS-x64.iso
$(BUILD)/GTOS-x64.iso: $(BUILD)/kernel.elf
	mkdir -p $(BUILD)/iso/boot/grub
	cp $< $(BUILD)/iso/boot/kernel.elf
	printf 'set timeout=0\nset default=0\nmenuentry "GTOS x64 BSP foundation" {\n multiboot2 /boot/kernel.elf\n boot\n}\n' > $(BUILD)/iso/boot/grub/grub.cfg
	$(GRUB_MKRESCUE) -o $@ $(BUILD)/iso
test-host: | $(BUILD)
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=address,undefined -Iarch/x86_64 arch/x86_64/handoff.c arch/x86_64/tests/handoff_test.c -o $(BUILD)/handoff-test
	ASAN_OPTIONS=detect_leaks=0 $(BUILD)/handoff-test
test: test-host
	$(PYTHON) arch/x86_64/tests/boot_qemu.py
clean:
	rm -rf $(BUILD)
