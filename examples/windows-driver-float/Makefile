# Windows Kernel Driver + Floating-Point/SIMD
# Usage: neverc make               (x64, the default)
#        neverc make ARCH=arm64
#        neverc make all-arch      (both)
#        neverc make TESTSIGN=1    (attach an Authenticode test signature)
#
# On x64 the kernel does NOT save/restore XMM registers on context switch.
# Bracket FP/SIMD usage with KeSaveExtendedProcessorState /
# KeRestoreExtendedProcessorState.

NEVERC ?= ../../build-neverc/bin/neverc

ARCH   ?= x64

ifeq ($(ARCH),x64)
TARGET = x86_64-pc-windows-msvc
else ifeq ($(ARCH),arm64)
TARGET = aarch64-pc-windows-msvc
else
$(error ARCH must be x64 or arm64, got '$(ARCH)')
endif

DRIVER  = FloatDriver-$(ARCH).sys

SRCS    = driver.c
HEADERS = config.h

# -fms-kernel selects the WDK headers and import libraries for the target
# architecture, and defines the environment they expect (_AMD64_ / _ARM64_ and
# _KERNEL_MODE), so none of that has to be restated here.
# --driver marks the image as kernel-mode: it makes the code and data sections
# non-paged (a driver touching paged memory at raised IRQL bugchecks), moves the
# import tables into the discardable INIT section, and fills in the PE checksum
# the kernel loader verifies.
# -g: emit DWARF debug info; remove for release builds
FLAGS = \
	--target=$(TARGET) \
	-g \
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
# is what lets it load on a machine with test signing on.
ifneq ($(TESTSIGN),)
FLAGS += -ftest-sign
endif

all: $(DRIVER)

$(DRIVER): $(SRCS) $(HEADERS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

all-arch:
	$(MAKE) ARCH=x64
	$(MAKE) ARCH=arm64

clean:
	rm -f FloatDriver-x64.sys FloatDriver-arm64.sys FloatDriver.sys

.PHONY: all all-arch clean
