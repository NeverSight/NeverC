# Multi-file Android GKI kernel module (.ko)
# Demonstrates NVK_BOOTSTRAP() only needs to be called once.
#
# Usage: neverc make

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

TARGET  = aarch64-linux-android
MODULE  = nvk_multi.ko
MODNAME = neverc_krt_multi
SRCS    = main.c interposes.c utils.c

# PROFILE=debug|release selects debuggable or distribution output.
# Command-line values win; otherwise reuse the stamp so `make push` never
# silently changes the module profile. Do not use ?= — exported environment
# variables would otherwise bypass the persisted selection.
# Persist via $(file): no host shell, so `neverc make` works on Windows too.
FLAGS_STAMP := .nvk-build-flags
RELEASE_BUNDLE_STAMP := .nvk-release-bundle
SAVED_BUILD_ID := $(file <$(FLAGS_STAMP))
SAVED_KERNEL := $(patsubst KERNEL=%,%,$(filter KERNEL=%,$(SAVED_BUILD_ID)))
SAVED_PROFILE := $(patsubst PROFILE=%,%,$(filter PROFILE=%,$(SAVED_BUILD_ID)))
ifneq ($(origin KERNEL),command line)
KERNEL := $(if $(SAVED_KERNEL),$(SAVED_KERNEL),510)
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
	-Wall -Wno-unused

# neverc make only rebuilds on prerequisite mtimes; KERNEL/PROFILE are not
# ordinary files, so persist both in one generated prerequisite.
BUILD_ID := KERNEL=$(KERNEL) PROFILE=$(PROFILE)
ifneq ($(strip $(file <$(FLAGS_STAMP))),$(BUILD_ID))
_ := $(file >$(FLAGS_STAMP),$(BUILD_ID))
endif

ifeq ($(PROFILE),release)
all:
	$(MAKE) PROFILE=release $(MODULE)
	$(MAKE) PROFILE=release $(MODULE).symbols.json
else
all: $(MODULE)
endif

# Re-enter with an explicit PROFILE so later bare `make push`/`run` keep using
# the same artifact instead of falling back to the default profile.
debug:
	$(MAKE) PROFILE=debug all

release:
	$(MAKE) PROFILE=release all

ifeq ($(PROFILE),release)
.PHONY: release-bundle-output

$(RELEASE_BUNDLE_STAMP): $(SRCS) $(FLAGS_STAMP)
	$(NEVERC) $(FLAGS) -r -nostdlib -o $(MODULE) $(SRCS) && echo $(BUILD_ID) > $@

release-bundle-output: $(RELEASE_BUNDLE_STAMP)
	@if test -f $(MODULE) && test -f $(MODULE).symbols.json; then :; else \
		$(NEVERC) $(FLAGS) -r -nostdlib -o $(MODULE) $(SRCS) && \
		echo $(BUILD_ID) > $(RELEASE_BUNDLE_STAMP); \
	fi

$(MODULE) $(MODULE).symbols.json: release-bundle-output
else
$(MODULE): $(SRCS) $(FLAGS_STAMP)
	$(NEVERC) $(FLAGS) -r -nostdlib -o $@ $(SRCS)
endif

push: $(MODULE)
	adb push $(MODULE) /data/local/tests/

insmod: push
	adb shell su -c 'insmod /data/local/tests/$(MODULE)'

rmmod:
	adb shell su -c 'rmmod $(MODNAME)'

run: insmod
	adb shell su -c 'dmesg | tail -10'

clean:
	rm -f $(MODULE) $(MODULE).symbols.json *.o $(FLAGS_STAMP) $(RELEASE_BUNDLE_STAMP)

.PHONY: all debug release push insmod rmmod run clean
