# Android GKI kernel chardev + ioctl demo (.ko)
# Usage: neverc make            (or: neverc make KERNEL=601 for 6.1, etc.)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_chardev.ko
MODNAME = neverc_krt_chardev
SRCS    = main.c

# -DNVK_KERNEL=510|515|601|606|612|618 selects the target kernel preset.
# Command-line KERNEL= wins; else last stamp (so `make push` keeps it); else 510.
# Do not use ?= — an exported KERNEL in the environment would ignore the stamp.
FLAGS_STAMP := .nvk-build-flags
SAVED_KERNEL := $(patsubst KERNEL=%,%,$(filter KERNEL=%,$(file <$(FLAGS_STAMP))))
ifneq ($(origin KERNEL),command line)
KERNEL := $(if $(SAVED_KERNEL),$(SAVED_KERNEL),510)
endif

FLAGS = \
	--target=$(TARGET) \
	-fandroid-kernel-driver-mode \
	-DNVK_KERNEL=$(KERNEL) \
	-Wall -Wno-unused

# neverc make only rebuilds on prerequisite mtimes; KERNEL is not a dep.
BUILD_ID := KERNEL=$(KERNEL)
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

run: insmod
	adb shell su -c 'cat /proc/$(MODNAME)'

clean:
	rm -f $(MODULE) *.o $(FLAGS_STAMP)

.PHONY: all push insmod rmmod run clean
