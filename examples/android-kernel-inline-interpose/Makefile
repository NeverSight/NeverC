# Android GKI kernel function-interpose demo (.ko)
#
# Demonstrates neverc_krt_interpose_register — function-entry interpose with auto-chain.
#
# Usage:
#   neverc make                   (default KERNEL=510)
#   neverc make KERNEL=606        (target 6.6)

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

TARGET  = aarch64-linux-android
MODULE  = nvk_interpose_demo.ko
MODNAME = neverc_krt_interpose_demo
SRCS    = main.c
NVK_ENABLE_EXTRA := 1

NVK_ROOT_MAKEFILE := $(MAKEFILE_LIST)
NVK_COMMON_MAKEFILE ?= ../android-kernel-common.mk
include $(NVK_COMMON_MAKEFILE)

push: $(MODULE)
	adb push $(MODULE) /data/local/tests/

insmod: push
	adb shell su -c 'insmod /data/local/tests/$(MODULE)'

rmmod:
	adb shell su -c 'rmmod $(MODNAME)'

log:
	adb shell su -c 'dmesg | grep neverc_krt_interpose_demo | tail -20'

run: insmod
	@sleep 1
	@$(MAKE) log

.PHONY: push insmod rmmod log run
