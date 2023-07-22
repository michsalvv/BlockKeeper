obj-m += blockkeper.o
blockkeper-objs += src/blockkeper.o src/dir.o src/file.o src/driver.o src/utils.o lib/scth.o 

FS_NAME := blockkeeper_fs

EXTRA_CFLAGS   += -I$(PWD)/include
NBLOCKS = 52
WB_DAEMON = 1

ifeq ($(WB_DAEMON), 0)
	KCPPFLAGS := "-DNUM_BLOCKS=$(NBLOCKS) -DWB_DAEMON=$(WB_DAEMON)"
else
	KCPPFLAGS := "-DNUM_BLOCKS=$(NBLOCKS)"
endif


# https://stackoverflow.com/questions/15430921/how-to-pass-parameters-from-makefile-to-linux-kernel-module-source-code


SYSCALL_TBL_ADDR = $(shell sudo -s cat /sys/module/the_usctm/parameters/sys_call_table_address)


.PHONY: user install all build clean

all: make-fs build install

install:
	sudo insmod blockkeper.ko the_syscall_table=$(SYSCALL_TBL_ADDR) && \
	sudo mount -o loop -t $(FS_NAME) image $(PWD)/mount

uninstall: dev-umount
	sudo rmmod blockkeper.ko

dev-mount:
	sudo mount -o loop -t $(FS_NAME) image $(PWD)/mount

dev-umount:
	sudo umount $(PWD)/mount

build:
	KCPPFLAGS=$(KCPPFLAGS) make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules 

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

user:
	gcc user/blockkeeper_cli.c user/error_handler.c -o blockkeeper_cli

test:
	gcc user/blockkeeper_tests.c user/error_handler.c -DNUM_BLOCKS=$(NBLOCKS) -o blockkeeper_tests

make-fs:
	rm -f image
	dd bs=4096 count=$(NBLOCKS) if=/dev/zero of=image
	gcc $(EXTRA_CFLAGS) src/makefs.c -o out/makefs && \
	./out/makefs image
