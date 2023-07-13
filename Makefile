obj-m += block_lvl_dev.o
block_lvl_dev-objs += src/block_lvl_dev.o src/dir.o src/file.o src/driver.o lib/scth.o

FS_NAME := blockkeeper_fs

EXTRA_CFLAGS   += -I$(PWD)/include
N_BLOCKS = 52

ifeq ($(WB_DAEMON), 1)
	KCPPFLAGS := "-DNUM_BLOCKS=$(N_BLOCKS) -DWB_DAEMON=$(WB_DAEMON)"
else
	KCPPFLAGS := "-DNUM_BLOCKS=$(N_BLOCKS)"
endif


# https://stackoverflow.com/questions/15430921/how-to-pass-parameters-from-makefile-to-linux-kernel-module-source-code


A = $(shell cat /sys/module/the_usctm/parameters/sys_call_table_address)


.PHONY: user

all:
	KCPPFLAGS=$(KCPPFLAGS) make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

compile:
	gcc $(EXTRA_CFLAGS) src/makefs.c -o out/makefs

user:
	gcc user/user.c -o out/user

make-fs:
	rm image 2>/dev/null
	dd bs=4096 count=$(N_BLOCKS) if=/dev/zero of=image
	./out/makefs image

umount-dev:
	sudo umount $(PWD)/mount

mount-dev:
	sudo mount -o loop -t $(FS_NAME) image $(PWD)/mount

insmod:
	insmod block_lvl_dev.ko the_syscall_table=$(A)
