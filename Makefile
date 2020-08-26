all:
	mkdir -p build
	cd build && cmake .. && make 

dump: 
	objdump -S build/kernel > build/kernel.dump
	# objdump -S build/user > build/user.dump

qemu:
	qemu-system-x86_64 -cpu Haswell -smp 1 -monitor stdio -hda build/kernel.iso -hdb hd80.img -m 8192M -s -S

qemu-gdb:
	nohup qemu-system-x86_64 -cdrom build/kernel.iso -serial stdio -m 64M -s -S & 
	gdb ./build/kernel --x ./gdbstart

bochs: 
	bochs -qf bochsrc
	
clean:
	rm -rf build
