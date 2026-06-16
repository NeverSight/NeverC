# Android GKI kernel module (.ko) — minimal, cross-compiled from any host
# Usage: neverc make
#
# No kernel source tree / kernel build system required: neverc compiles against
# the bundled minimal kernel runtime (runtime/android/kernel) and relocatably
# links the .ko itself.

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_hello.ko
SRCS    = main.c

# -DNVK_KERNEL=510|515|601|606|612 selects the target kernel preset (default 510).
KERNEL ?= 510

FLAGS = \
	--target=$(TARGET) \
	-fandroid-kernel-driver-mode \
	-DNVK_KERNEL=$(KERNEL) \
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
	adb shell su -c 'dmesg | tail -5'

clean:
	rm -f $(MODULE) *.o

.PHONY: all push insmod rmmod run clean
