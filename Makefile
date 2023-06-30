obj-m += block_lvl_dev.o
block_lvl_dev-objs += src/block_lvl_dev.o src/dir.o src/file.o src/driver.o lib/scth.o

EXTRA_CFLAGS   += -I$(PWD)/include
N_BLOCKS = 12

# https://stackoverflow.com/questions/15430921/how-to-pass-parameters-from-makefile-to-linux-kernel-module-source-code
KCPPFLAGS := "-DNUM_BLOCKS=$(N_BLOCKS)"

A = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)

all:
	KCPPFLAGS=$(KCPPFLAGS) make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

compile:
	gcc $(EXTRA_CFLAGS) src/makefs.c -o makefs

make-fs:
	rm image 2>/dev/null
	dd bs=4096 count=$(N_BLOCKS) if=/dev/zero of=image
	./makefs image

umount-dev:
	sudo umount $(PWD)/mount

mount-dev:
	sudo mount -o loop -t sf_blk_fs image $(PWD)/mount

insmod:
	insmod block_lvl_dev.ko the_syscall_table=$(A)
