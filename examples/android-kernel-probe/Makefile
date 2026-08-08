# Android GKI kernel probe demo (.ko)
#
# Demonstrates neverc_krt_probe_register — arbitrary-point interpose with ctx mode.
#
# Usage:
#   neverc make                   (default KERNEL=510)
#   neverc make KERNEL=606        (target 6.6)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_probe.ko
MODNAME = neverc_krt_probe_demo
SRCS    = main.c

KERNEL ?= 510
EXTRA  ?=

FLAGS = \
	--target=$(TARGET) \
	-fandroid-kernel-driver-mode \
	-DNVK_KERNEL=$(KERNEL) \
	$(EXTRA) \
	-Wall -Wno-unused

# neverc make only rebuilds on prerequisite mtimes; KERNEL/EXTRA are not deps.
FLAGS_STAMP := .nvk-build-flags
BUILD_ID := KERNEL=$(KERNEL) EXTRA=$(EXTRA)
ifneq ($(strip $(file <$(FLAGS_STAMP))),$(BUILD_ID))
_ := $(file >$(FLAGS_STAMP),$(BUILD_ID))
endif

all: $(MODULE)

$(MODULE): $(SRCS) $(FLAGS_STAMP)
	$(NEVERC) $(FLAGS) -r -nostdlib -o $@ $(SRCS)

push: $(MODULE)
	adb push $(MODULE) /data/local/tests/

insmod: push
	adb shell su -c 'insmod /data/local/tests/$(MODULE)'

rmmod:
	adb shell su -c 'rmmod $(MODNAME)'

log:
	adb shell su -c 'dmesg | grep neverc_krt_probe | tail -20'

run: insmod
	@sleep 1
	@$(MAKE) log

clean:
	rm -f $(MODULE) *.o $(FLAGS_STAMP)

.PHONY: all push insmod rmmod log run clean
