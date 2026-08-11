# Android GKI kernel netlink IPC demo (.ko)
#
# Usage:
#   neverc make                       (default kernel 510)
#   neverc make KERNEL=606            (target 6.6)

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

TARGET  = aarch64-linux-android
MODULE  = nvk_netlink.ko
MODNAME = neverc_krt_netlink
SRCS    = main.c

# PROFILE=debug|release selects debuggable or distribution output.
# Command-line KERNEL=/EXTRA=/PROFILE= win; else last stamp (so `make push`
# keeps them). Do not use ?= — exported environment variables would otherwise
# bypass the persisted selection. Persist via $(file): no host shell, so
# `neverc make` works on Windows too.
FLAGS_STAMP := .nvk-build-flags
EXTRA_STAMP := .nvk-build-extra
SAVED_BUILD_ID := $(file <$(FLAGS_STAMP))
SAVED_KERNEL := $(patsubst KERNEL=%,%,$(filter KERNEL=%,$(SAVED_BUILD_ID)))
SAVED_EXTRA := $(file <$(EXTRA_STAMP))
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

# neverc make only rebuilds on prerequisite mtimes. Persist EXTRA separately
# so a multi-word flag list can be restored without whitespace tokenization.
BUILD_ID := KERNEL=$(KERNEL) PROFILE=$(PROFILE) NEVERC=$(NEVERC)
MAKE_MODE_FLAGS := $(firstword -$(MAKEFLAGS))
SKIP_BUILD_RECORD := $(or $(findstring n,$(MAKE_MODE_FLAGS)),$(findstring q,$(MAKE_MODE_FLAGS)),$(findstring t,$(MAKE_MODE_FLAGS)))
REBUILD_CONFIG :=
ifneq ($(strip $(file <$(FLAGS_STAMP))),$(BUILD_ID))
REBUILD_CONFIG := force-config-rebuild
endif
ifneq ($(file <$(EXTRA_STAMP)),$(EXTRA))
REBUILD_CONFIG := force-config-rebuild
endif

all: $(MODULE)

# Re-enter with an explicit PROFILE so later bare `make push`/`run` keep using
# the same artifact instead of falling back to the default profile. Explicit
# profile targets always rebuild once so coarse timestamps cannot preserve the
# wrong profile and a release can repair a missing or mismatched sidecar.
debug:
	$(MAKE) -B PROFILE=debug NEVERC="$(NEVERC)" EXTRA="$(EXTRA)" all

release:
	$(MAKE) -B PROFILE=release NEVERC="$(NEVERC)" EXTRA="$(EXTRA)" all

$(MODULE): $(SRCS) $(REBUILD_CONFIG)
	"$(NEVERC)" $(FLAGS) -r -nostdlib -o $@ $(SRCS)
	$(if $(SKIP_BUILD_RECORD),,$(file >$(FLAGS_STAMP),$(BUILD_ID)))
	$(if $(SKIP_BUILD_RECORD),,$(file >$(EXTRA_STAMP),$(EXTRA)))

force-config-rebuild:

push: $(MODULE)
	adb push $(MODULE) /data/local/tests/

insmod: push
	adb shell su -c 'insmod /data/local/tests/$(MODULE)'

rmmod:
	adb shell su -c 'rmmod $(MODNAME)'

run: insmod
	adb shell su -c 'dmesg | tail -5'

clean:
	rm -f $(MODULE) $(MODULE).symbols.json *.o $(FLAGS_STAMP) $(EXTRA_STAMP) .nvk-release-bundle

.PHONY: all debug release push insmod rmmod run clean force-config-rebuild
