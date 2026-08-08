# Android GKI kernel lowvis demo (.ko)
#
# Usage:
#   neverc make                       (default kernel 510)
#   neverc make KERNEL=606            (target 6.6)

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

TARGET  = aarch64-linux-android
MODULE  = nvk_lowvis.ko
MODNAME = neverc_krt_lowvis
SRCS    = main.c

# PROFILE=debug|release selects debuggable or distribution output.
# Command-line KERNEL=/EXTRA=/PROFILE= win; else last stamp (so `make push`
# keeps them). Do not use ?= — exported environment variables would otherwise
# bypass the persisted selection. Persist via $(file): no host shell, so
# `neverc make` works on Windows too.
FLAGS_STAMP := .nvk-build-flags
SAVED_BUILD_ID := $(file <$(FLAGS_STAMP))
SAVED_KERNEL := $(patsubst KERNEL=%,%,$(filter KERNEL=%,$(SAVED_BUILD_ID)))
SAVED_EXTRA := $(patsubst EXTRA=%,%,$(filter EXTRA=%,$(SAVED_BUILD_ID)))
SAVED_PROFILE := $(patsubst PROFILE=%,%,$(filter PROFILE=%,$(SAVED_BUILD_ID)))
ifneq ($(origin KERNEL),command line)
KERNEL := $(if $(SAVED_KERNEL),$(SAVED_KERNEL),510)
endif
ifneq ($(origin EXTRA),command line)
EXTRA := $(SAVED_EXTRA)
endif
ifneq ($(origin PROFILE),command line)
PROFILE := $(if $(SAVED_PROFILE),$(SAVED_PROFILE),debug)
endif

VALID_PROFILES := debug release
ifeq ($(filter $(PROFILE),$(VALID_PROFILES)),)
$(error unsupported PROFILE '$(PROFILE)'; expected one of: $(VALID_PROFILES))
endif

PROFILE_FLAGS_debug   := -g
PROFILE_FLAGS_release := -O2 --strip
PROFILE_FLAGS := $(PROFILE_FLAGS_$(PROFILE))

FLAGS = \
	--target=$(TARGET) \
	-fandroid-kernel-driver-mode \
	-DNVK_KERNEL=$(KERNEL) \
	$(PROFILE_FLAGS) \
	$(EXTRA) \
	-Wall -Wno-unused

# neverc make only rebuilds on prerequisite mtimes; KERNEL/EXTRA/PROFILE are not
# ordinary files, so persist them in one generated prerequisite.
BUILD_ID := KERNEL=$(KERNEL) EXTRA=$(EXTRA) PROFILE=$(PROFILE)
ifneq ($(strip $(file <$(FLAGS_STAMP))),$(BUILD_ID))
_ := $(file >$(FLAGS_STAMP),$(BUILD_ID))
endif

all: $(MODULE)

# Re-enter with an explicit PROFILE so later bare `make push`/`run` keep using
# the same artifact instead of falling back to the default profile.
debug:
	$(MAKE) PROFILE=debug all

release:
	$(MAKE) PROFILE=release all

$(MODULE): $(SRCS) $(FLAGS_STAMP)
	$(NEVERC) $(FLAGS) -r -nostdlib -o $@ $(SRCS)

push: $(MODULE)
	adb push $(MODULE) /data/local/tests/

insmod: push
	adb shell su -c 'insmod /data/local/tests/$(MODULE)'

rmmod:
	adb shell su -c 'rmmod $(MODNAME)'

run: insmod
	@echo "Module loaded. Check visibility: adb shell su -c 'lsmod | grep $(MODNAME)'"

clean:
	rm -f $(MODULE) *.o $(FLAGS_STAMP)

.PHONY: all debug release push insmod rmmod run clean
