# Windows Kernel Driver — minimal WDM driver cross-compiled from any host
# Usage: neverc make               (x64, the default)
#        neverc make ARCH=arm64
#        neverc make all-arch      (both)
#        neverc make TESTSIGN=1    (attach an Authenticode test signature)
#        neverc make release       (-O2 --strip)

NEVERC ?= ../../build-neverc/bin/neverc
.DEFAULT_GOAL := all

# ARCH/PROFILE/TESTSIGN: command line wins; else last stamp; else defaults.
# Do not use ?= — an exported env var would otherwise ignore the stamp.
# Persist via $(file): no host shell, so `neverc make` works on Windows too.
FLAGS_STAMP := .nvk-build-flags
SAVED_BUILD_ID := $(file <$(FLAGS_STAMP))
SAVED_ARCH := $(patsubst ARCH=%,%,$(filter ARCH=%,$(SAVED_BUILD_ID)))
SAVED_PROFILE := $(patsubst PROFILE=%,%,$(filter PROFILE=%,$(SAVED_BUILD_ID)))
SAVED_TESTSIGN := $(patsubst TESTSIGN=%,%,$(filter TESTSIGN=%,$(SAVED_BUILD_ID)))
ifneq ($(origin ARCH),command line)
ARCH := $(if $(SAVED_ARCH),$(SAVED_ARCH),x64)
endif
ifneq ($(origin PROFILE),command line)
PROFILE := $(if $(SAVED_PROFILE),$(SAVED_PROFILE),debug)
endif
ifneq ($(origin TESTSIGN),command line)
TESTSIGN := $(SAVED_TESTSIGN)
endif

ifeq ($(ARCH),x64)
TARGET = x86_64-pc-windows-msvc
else ifeq ($(ARCH),arm64)
TARGET = aarch64-pc-windows-msvc
else
$(error ARCH must be x64 or arm64, got '$(ARCH)')
endif

VALID_PROFILES := debug release
ifeq ($(filter $(PROFILE),$(VALID_PROFILES)),)
$(error unsupported PROFILE '$(PROFILE)'; expected one of: $(VALID_PROFILES))
endif

PROFILE_FLAGS_debug   := -g
PROFILE_FLAGS_release := -O2 --strip
PROFILE_FLAGS := $(PROFILE_FLAGS_$(PROFILE))

DRIVER  = ExampleDriver-$(ARCH).sys

SRCS    = driver.c
HEADERS = config.h

# -fms-kernel selects the WDK headers and import libraries for the target
# architecture, and defines the environment they expect (_AMD64_ / _ARM64_ and
# _KERNEL_MODE), so none of that has to be restated here.
# --driver marks the image as kernel-mode: it makes the code and data sections
# non-paged (a driver touching paged memory at raised IRQL bugchecks), moves the
# import tables into the discardable INIT section, and fills in the PE checksum
# the kernel loader verifies.
FLAGS = \
	--target=$(TARGET) \
	$(PROFILE_FLAGS) \
	-fms-kernel \
	-D_CRT_SECURE_NO_WARNINGS \
	-Wall \
	-nostdlib \
	-shared \
	-Xlinker --driver \
	-Xlinker --entry=DriverEntry \
	-Xlinker --subsystem=native \
	-Xlinker --nodefaultlib \
	-lntoskrnl -lhal

# TESTSIGN=1 signs the image with the compiler's built-in test identity, which
# is what lets it load on a machine with test signing on.  See the README for
# the one-time setup that machine needs. Signing runs after strip in the same
# link, so release + TESTSIGN=1 is the correct order.
ifneq ($(TESTSIGN),)
FLAGS += -ftest-sign
endif

BUILD_ID := ARCH=$(ARCH) PROFILE=$(PROFILE) TESTSIGN=$(TESTSIGN)
ifneq ($(strip $(file <$(FLAGS_STAMP))),$(BUILD_ID))
_ := $(file >$(FLAGS_STAMP),$(BUILD_ID))
endif

all: $(DRIVER)

debug:
	$(MAKE) PROFILE=debug all

release:
	$(MAKE) PROFILE=release all

$(DRIVER): $(SRCS) $(HEADERS) $(FLAGS_STAMP)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

all-arch:
	$(MAKE) ARCH=x64
	$(MAKE) ARCH=arm64

clean:
	rm -f ExampleDriver-x64.sys ExampleDriver-arm64.sys ExampleDriver.sys $(FLAGS_STAMP)

.PHONY: all debug release all-arch clean
