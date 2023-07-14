obj-m += blockkeper.o
blockkeper-objs += src/blockkeper.o src/dir.o src/file.o src/driver.o src/utils.o lib/scth.o 

FS_NAME := blockkeeper_fs

EXTRA_CFLAGS   += -I$(PWD)/include
N_BLOCKS = 52

ifeq ($(WB_DAEMON), 1)
	KCPPFLAGS := "-DNUM_BLOCKS=$(N_BLOCKS) -DWB_DAEMON=$(WB_DAEMON)"
else
	KCPPFLAGS := "-DNUM_BLOCKS=$(N_BLOCKS)"
endif


# https://stackoverflow.com/questions/15430921/how-to-pass-parameters-from-makefile-to-linux-kernel-module-source-code


SYSCALL_TBL_ADDR = $(shell sudo -s cat /sys/module/the_usctm/parameters/sys_call_table_address)


.PHONY: user install all build clean

all: make-fs build install

install:
	sudo insmod blockkeper.ko the_syscall_table=$(SYSCALL_TBL_ADDR) && \
	sudo mount -o loop -t $(FS_NAME) image $(PWD)/mount

uninstall:
	sudo umount $(PWD)/mount && \
	sudo rmmod blockkeper.ko

build:
	KCPPFLAGS=$(KCPPFLAGS) make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

user:
	gcc user/test.c -o out/test

make-fs:
	rm image 2>/dev/null
	dd bs=4096 count=$(N_BLOCKS) if=/dev/zero of=image
	gcc $(EXTRA_CFLAGS) src/makefs.c -o out/makefs && \
	./out/makefs image