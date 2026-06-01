# Windows Kernel Driver + Floating-Point/SIMD
# Usage: neverc make
#
# The kernel does NOT save/restore XMM registers on context switch.
# Bracket FP/SIMD usage with KeSaveExtendedProcessorState /
# KeRestoreExtendedProcessorState.

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
DRIVER  = FloatDriver.sys

SRCS    = driver.c
HEADERS = config.h

FLAGS = \
	--target=$(TARGET) \
	-g \
	-fms-kernel \
	-D_AMD64_ \
	-DNTDDI_VERSION=0x06010000 \
	-D_WIN32_WINNT=0x0601 \
	-D_CRT_SECURE_NO_WARNINGS \
	-Wall \
	-nostdlib \
	-shared \
	-Xlinker --entry=DriverEntry \
	-Xlinker --subsystem=native \
	-Xlinker --nodefaultlib \
	-lntoskrnl -lhal

all: $(DRIVER)

$(DRIVER): $(SRCS) $(HEADERS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(DRIVER)

.PHONY: all clean
