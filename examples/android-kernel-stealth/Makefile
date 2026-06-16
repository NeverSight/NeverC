# Android GKI kernel stealth demo (.ko)
#
# Usage:
#   neverc make                       (default kernel 510)
#   neverc make KERNEL=606            (target 6.6)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_stealth.ko
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
	@echo "Module loaded and hidden. Check: adb shell su -c 'lsmod | grep nvk'"

clean:
	rm -f $(MODULE) *.o

.PHONY: all push insmod rmmod run clean
