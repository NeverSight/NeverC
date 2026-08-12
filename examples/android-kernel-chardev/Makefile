# Android GKI kernel chardev + ioctl demo (.ko)
# Usage: neverc make            (or: neverc make KERNEL=601 for 6.1, etc.)

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

ifneq ($(origin NEVERC_MAKE_EXECUTABLE),default)
$(error this Makefile requires 'neverc make'; external make is unsupported)
endif

# Profile-changing and destructive goals cannot safely share one invocation:
# their recipes re-enter make with different state or remove shared outputs.
ifneq ($(filter debug release clean,$(MAKECMDGOALS)),)
ifneq ($(word 2,$(MAKECMDGOALS)),)
$(error debug/release/clean must each be invoked as the sole goal)
endif
endif

TARGET  = aarch64-linux-android
MODULE  = nvk_chardev.ko
MODNAME = neverc_krt_chardev
SRCS    = main.c

# -DNVK_KERNEL=510|515|601|606|612|618 selects the target kernel preset.
# PROFILE=debug|release selects debuggable or distribution output.
# Command-line values win; otherwise reuse the stamp so `make push` never
# silently changes the module profile. Do not use ?= — exported environment
# variables would otherwise bypass the persisted selection.
# The compiler publishes this state in the module/map output transaction, so a
# failed build cannot replace the saved selection. The integrity stamp binds
# that selection to the exact module bytes after an interrupted publication.
FLAGS_STAMP := $(dir $(MODULE)).nvk-build-flags
EXTRA_STAMP := $(dir $(MODULE)).nvk-build-extra
INTEGRITY_STAMP := $(dir $(MODULE)).nvk-build-integrity
RECORDED_BUILD_INTEGRITY_BEFORE := $(file <$(INTEGRITY_STAMP))
SAVED_BUILD_ID_CANDIDATE := $(file <$(FLAGS_STAMP))
SAVED_EXTRA_CANDIDATE := $(file <$(EXTRA_STAMP))
CURRENT_BUILD_INTEGRITY := $(shell $(NEVERC_MAKE_EXECUTABLE) __neverc_android_kernel_output_integrity "$(MODULE)")
BUILD_ID_AFTER := $(file <$(FLAGS_STAMP))
EXTRA_AFTER := $(file <$(EXTRA_STAMP))
RECORDED_BUILD_INTEGRITY_AFTER := $(file <$(INTEGRITY_STAMP))
SAVED_BUILD_ID :=
SAVED_EXTRA :=
ifneq ($(CURRENT_BUILD_INTEGRITY),)
ifeq ($(RECORDED_BUILD_INTEGRITY_BEFORE),$(CURRENT_BUILD_INTEGRITY))
ifeq ($(RECORDED_BUILD_INTEGRITY_AFTER),$(CURRENT_BUILD_INTEGRITY))
ifeq ($(SAVED_BUILD_ID_CANDIDATE),$(BUILD_ID_AFTER))
ifeq ($(SAVED_EXTRA_CANDIDATE),$(EXTRA_AFTER))
SAVED_BUILD_ID := $(SAVED_BUILD_ID_CANDIDATE)
SAVED_EXTRA := $(SAVED_EXTRA_CANDIDATE)
endif
endif
endif
endif
endif
SAVED_KERNEL := $(patsubst KERNEL=%,%,$(filter KERNEL=%,$(SAVED_BUILD_ID)))
SAVED_PROFILE := $(patsubst PROFILE=%,%,$(filter PROFILE=%,$(SAVED_BUILD_ID)))
ifeq ($(origin NVK_RECURSIVE_BUILD):$(NVK_RECURSIVE_BUILD),command line:1)
KERNEL := $(NVK_RECURSIVE_KERNEL)
NEVERC := $(NVK_RECURSIVE_NEVERC)
else
ifneq ($(origin KERNEL),command line)
KERNEL := $(if $(SAVED_KERNEL),$(SAVED_KERNEL),510)
endif
endif
ifneq ($(origin PROFILE),command line)
PROFILE := $(if $(SAVED_PROFILE),$(SAVED_PROFILE),debug)
endif
export NVK_RECURSIVE_KERNEL := $(KERNEL)
export NVK_RECURSIVE_NEVERC := $(NEVERC)

ifneq ($(MAKECMDGOALS),clean)
VALID_PROFILES := debug release
ifneq ($(PROFILE),debug)
ifneq ($(PROFILE),release)
$(error unsupported PROFILE '$(PROFILE)'; expected one of: $(VALID_PROFILES))
endif
endif
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
# ordinary files, so persist them and the compiler path in one prerequisite.
BUILD_ID := KERNEL=$(KERNEL) PROFILE=$(PROFILE) NEVERC=$(NEVERC)
export NEVERC_ANDROID_KERNEL_BUILD_ID := $(BUILD_ID)
export NEVERC_ANDROID_KERNEL_BUILD_EXTRA :=
MAKE_MODE_FLAGS := $(firstword -$(MAKEFLAGS))
RECURSIVE_MAKE_FLAGS := $(if $(findstring n,$(MAKE_MODE_FLAGS)),-n,) $(if $(findstring k,$(MAKE_MODE_FLAGS)),-k,) $(if $(findstring s,$(MAKE_MODE_FLAGS)),-s,)
REBUILD_CONFIG :=
ifneq ($(SAVED_BUILD_ID),$(BUILD_ID))
REBUILD_CONFIG := force-config-rebuild
endif
ifeq ($(PROFILE),release)
ifeq ($(wildcard $(MODULE).symbols.json),)
REBUILD_CONFIG := force-config-rebuild
endif
else
ifneq ($(wildcard $(MODULE).symbols.json),)
REBUILD_CONFIG := force-config-rebuild
endif
endif

all: $(MODULE)

# Re-enter with an explicit PROFILE so later bare `make push`/`run` keep using
# the same artifact instead of falling back to the default profile. Explicit
# profile targets always rebuild once so coarse timestamps cannot preserve the
# wrong profile and a release can repair a missing or mismatched sidecar.
debug:
	+$(MAKE) $(RECURSIVE_MAKE_FLAGS) -f "$(MAKEFILE_LIST)" -B NVK_RECURSIVE_BUILD=1 PROFILE=debug all

release:
	+$(MAKE) $(RECURSIVE_MAKE_FLAGS) -f "$(MAKEFILE_LIST)" -B NVK_RECURSIVE_BUILD=1 PROFILE=release all

$(MODULE): $(SRCS) $(REBUILD_CONFIG)
	"$(NEVERC)" $(FLAGS) -r -nostdlib -o $@ $(SRCS)

force-config-rebuild:

push: $(MODULE)
	adb push $(MODULE) /data/local/tests/

insmod: push
	adb shell su -c 'insmod /data/local/tests/$(MODULE)'

rmmod:
	adb shell su -c 'rmmod $(MODNAME)'

run: insmod
	adb shell su -c 'cat /proc/$(MODNAME)'

clean:
	$(NEVERC_MAKE_EXECUTABLE) __neverc_clean_android_kernel_output "$(MODULE)"
	rm -f *.o

.PHONY: all debug release push insmod rmmod run clean force-config-rebuild
