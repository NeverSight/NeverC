# Multi-file Android GKI kernel module (.ko)
# Demonstrates NVK_BOOTSTRAP() only needs to be called once.
#
# Usage: neverc make

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

TARGET  = aarch64-linux-android
MODULE  = nvk_multi.ko
MODNAME = neverc_krt_multi
SRCS    = main.c interposes.c utils.c

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
	adb shell su -c 'dmesg | tail -10'

.PHONY: push insmod rmmod run
