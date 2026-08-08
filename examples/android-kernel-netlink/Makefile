# Android GKI kernel netlink IPC demo (.ko)
#
# Usage:
#   neverc make                       (default kernel 510)
#   neverc make KERNEL=606            (target 6.6)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
MODULE  = nvk_netlink.ko
MODNAME = neverc_krt_netlink
SRCS    = main.c

# Command-line KERNEL=/EXTRA= win; else last stamp (so `make push` keeps them).
# Do not use ?= — an exported KERNEL/EXTRA in the environment would ignore the stamp.
FLAGS_STAMP := .nvk-build-flags
_STAMP := $(file <$(FLAGS_STAMP))
SAVED_KERNEL := $(patsubst KERNEL=%,%,$(filter KERNEL=%,$(_STAMP)))
SAVED_EXTRA := $(patsubst EXTRA=%,%,$(filter EXTRA=%,$(_STAMP)))
ifneq ($(origin KERNEL),command line)
KERNEL := $(if $(SAVED_KERNEL),$(SAVED_KERNEL),510)
endif
ifneq ($(origin EXTRA),command line)
EXTRA := $(SAVED_EXTRA)
endif

FLAGS = \
	--target=$(TARGET) \
	-fandroid-kernel-driver-mode \
	-DNVK_KERNEL=$(KERNEL) \
	$(EXTRA) \
	-Wall -Wno-unused

# neverc make only rebuilds on prerequisite mtimes; KERNEL/EXTRA are not deps.
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

run: insmod
	adb shell su -c 'dmesg | tail -5'

clean:
	rm -f $(MODULE) *.o $(FLAGS_STAMP)

.PHONY: all push insmod rmmod run clean
