GPPPARAMS = -m32 -fno-use-cxa-atexit -nostdlib -fno-builtin -fno-rtti -fno-exceptions -fno-leading-underscore
ASPARAMS = --32
LDPARAMS = -melf_i386

objects = loader.o gdt.o kernel.o

%.o : %.cpp
	g++ $(GPPPARAMS) -o $@ -c $<

%.o : %.s
	as $(ASPARAMS) -o $@ $<

GTOS.bin : linker.ld $(objects)
	ld $(LDPARAMS) -T $< -o $@ $(objects)

install : GTOS.bin
	sudo cp $< /boot/GTOS.bin

GTOS.iso: GTOS.bin
	mkdir iso
	mkdir iso/boot
	mkdir iso/boot/grub
	cp $< iso/boot/GTOS.bin
	cp gpl3.txt iso/
	echo 'set timeout=0'                 > iso/boot/grub/grub.cfg
	echo 'set default=0'                >> iso/boot/grub/grub.cfg
	echo ''                             >> iso/boot/grub/grub.cfg
	echo 'menuentry "KayOS" {'          >> iso/boot/grub/grub.cfg
	echo '  multiboot /boot/GTOS.bin'  >> iso/boot/grub/grub.cfg
	echo '  boot'                       >> iso/boot/grub/grub.cfg
	echo '}'                            >> iso/boot/grub/grub.cfg
	grub-mkrescue --output=$@ iso
	rm -rf iso
	
run : GTOS.iso
	VirtualBoxVM --startvm GTOS &

rm : GTOS.iso
	rm -f *.o *.bin *.iso