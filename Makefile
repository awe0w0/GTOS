GPPPARAMS = -m32 -Iinclude -fno-use-cxa-atexit -nostdlib -fno-builtin -fno-rtti -fno-exceptions -fno-leading-underscore -Wno-write-string
ASPARAMS = --32
LDPARAMS = -melf_i386

objects = 	obj/loader.o \
			obj/gdt.o \
			obj/memorymanagement.o \
			obj/drivers/driver.o \
			obj/hardwarecommunication/port.o \
			obj/hardwarecommunication/interruptstubs.o \
			obj/hardwarecommunication/interrupts.o \
			obj/drivers/amd_am79c973.o \
			obj/hardwarecommunication/pci.o \
			obj/drivers/keyboard.o \
			obj/drivers/mouse.o \
			obj/drivers/vga.o \
			obj/gui/widget.o \
			obj/gui/window.o \
			obj/gui/desktop.o \
			obj/multitasking.o \
			obj/kernel.o

obj/%.o : src/%.cpp
	mkdir -p $(@D)
	g++ $(GPPPARAMS) -o $@ -c $<

obj/%.o : src/%.s
	mkdir -p $(@D)
	as $(ASPARAMS) -o $@ $<

GTOS.bin : linker.ld $(objects)
	ld $(LDPARAMS) -T $< -o $@ $(objects)

install : GTOS.bin
	sudo cp $< /boot/GTOS.bin

GTOS.iso: GTOS.bin
	mkdir iso
	mkdir iso/boot
	mkdir iso/boot/grub
	cp GTOS.bin iso/boot/GTOS.bin
	echo 'set timeout=0' > iso/boot/grub/grub.cfg
	echo 'set default=0' >> iso/boot/grub/grub.cfg
	echo '' >> iso/boot/grub/grub.cfg
	echo 'menuentry "My Operating System"{' >> iso/boot/grub/grub.cfg
	echo 'multiboot /boot/GTOS.bin' >> iso/boot/grub/grub.cfg
	echo 'boot' >> iso/boot/grub/grub.cfg
	echo '}' >> iso/boot/grub/grub.cfg
	echo '' >> iso/boot/grub/grub.cfg
	grub-mkrescue --output=GTOS.iso iso
	rm -rf iso
	
run : GTOS.iso
	VirtualBoxVM --startvm GTOS &

.PHONY : rm
rm:
	rm -rf obj GTOS.bin GTOS.iso