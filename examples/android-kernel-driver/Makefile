# Android GKI kernel driver (.ko) — dynamic-kallsyms template, cross-host build
# Usage: neverc make            (or: neverc make KERNEL=601 for 6.1)

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

TARGET  = aarch64-linux-android
MODULE  = nvk_driver.ko
MODNAME = neverc_krt_driver
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
	adb shell su -c 'dmesg | tail -5'

.PHONY: push insmod rmmod run
