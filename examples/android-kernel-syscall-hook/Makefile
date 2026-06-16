# Android GKI kernel syscall-hook demo (.ko)
#
# Usage:
#   make                                       (method A: sys_call_table swap)
#   make EXTRA=-DNVK_SYSCALL_INLINE_HOOK       (method B: inline hook)
#   make KERNEL=601                            (target 6.1)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_syscall_hook.ko
SRCS    = main.c

# -DNVK_KERNEL=510|515|601|606|612 selects the target kernel preset.
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
