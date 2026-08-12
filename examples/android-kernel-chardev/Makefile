# Android GKI kernel chardev + ioctl demo (.ko)
# Usage: neverc make            (or: neverc make KERNEL=601 for 6.1, etc.)

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

TARGET  = aarch64-linux-android
MODULE  = nvk_chardev.ko
MODNAME = neverc_krt_chardev
SRCS    = main.c

NVK_ROOT_MAKEFILE := $(MAKEFILE_LIST)
NVK_COMMON_MAKEFILE ?= ../android-kernel-common.mk
include $(NVK_COMMON_MAKEFILE)

push: $(MODULE)
	adb push $(MODULE) /data/local/tests/

insmod: push
	adb shell su -c 'insmod /data/local/tests/$(MODULE)'

rmmod:
	adb shell su -c 'rmmod $(MODNAME)'

run: insmod
	adb shell su -c 'cat /proc/$(MODNAME)'

.PHONY: push insmod rmmod run
