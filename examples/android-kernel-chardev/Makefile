# Android GKI kernel chardev + ioctl demo (.ko)
# Usage: neverc make            (or: neverc make KERNEL=601 for 6.1, etc.)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_chardev.ko
MODNAME = neverc_krt_chardev
SRCS    = main.c

# -DNVK_KERNEL=510|515|601|606|612|618 selects the target kernel preset.
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
	adb shell su -c 'rmmod $(MODNAME)'

run: insmod
	adb shell su -c 'cat /proc/$(MODNAME)'

clean:
	rm -f $(MODULE) *.o

.PHONY: all push insmod rmmod run clean
