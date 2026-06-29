# Android GKI kernel syscall-hook demo (.ko)
#
# Demonstrates sys_call_table pointer replacement for syscall interception.
#
# Usage:
#   neverc make                   (default KERNEL=510)
#   neverc make KERNEL=606        (target 6.6)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_syscall_hook.ko
SRCS    = main.c

KERNEL ?= 510
EXTRA  ?=

FLAGS = \
	--target=$(TARGET) \
	-fandroid-kernel-driver-mode \
	-DNVK_KERNEL=$(KERNEL) \
	$(EXTRA) \
	-Wall -Wno-unused

all: $(MODULE)

$(MODULE): $(SRCS)
	$(NEVERC) $(FLAGS) -r -nostdlib -o $@ $(SRCS)

push: $(MODULE)
	adb push $(MODULE) /data/local/tests/

insmod: push
	adb shell su -c 'insmod /data/local/tests/$(MODULE)'

rmmod:
	adb shell su -c 'rmmod $(MODULE:.ko=)'

log:
	adb shell su -c 'dmesg | grep neverc_krt_syscall | tail -20'

run: insmod
	@sleep 1
	@$(MAKE) log

clean:
	rm -f $(MODULE) *.o

.PHONY: all push insmod rmmod log run clean
