# Android GKI kernel module (.ko) — minimal, cross-compiled from any host
# Usage: neverc make
#
# No kernel source tree / kernel build system required: neverc compiles against
# the bundled minimal kernel runtime (runtime/android/kernel) and relocatably
# links the .ko itself.

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

TARGET  = aarch64-linux-android
MODULE  = nvk_hello.ko
MODNAME = neverc_krt_hello
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
