# Android GKI kernel module (.ko) — minimal, cross-compiled from any host
# Usage: neverc make
#
# No kernel source tree / kernel build system required: neverc compiles against
# the bundled minimal kernel runtime (runtime/android/kernel) and relocatably
# links the .ko itself.

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_hello.ko
MODNAME = neverc_krt_hello
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
	adb shell su -c 'dmesg | tail -5'

clean:
	rm -f $(MODULE) *.o $(FLAGS_STAMP)

.PHONY: all push insmod rmmod run clean
