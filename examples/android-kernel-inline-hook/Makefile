# Android GKI kernel inline-hook demo (.ko)
#
# Usage:
#   neverc make                                       (simple hook mode)
#   neverc make EXTRA=-DNVK_CONTEXT_HOOK              (context hook mode)
#   neverc make KERNEL=601                            (target 6.1)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_inline_hook.ko
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

run: insmod
	adb shell su -c 'dmesg | tail -10'

clean:
	rm -f $(MODULE) *.o

.PHONY: all push insmod rmmod run clean
