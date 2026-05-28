NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
DRIVER  = FloatDriver.sys
SOURCES = driver.c

# Floating-point in kernel mode:
#   1. The compiler emits an undefined reference to _fltused whenever the
#      module touches floating-point. For -fms-kernel targets, LLVM defines
#      it locally as 0 (see llvm/lib/Target/X86/X86AsmPrinter.cpp), so no
#      external _fltused symbol is required.
#   2. The kernel does NOT save/restore XMM/YMM/ZMM registers on context
#      switch by default. Touching them from arbitrary kernel code would
#      corrupt user-mode state. Always bracket FP/SIMD usage with
#      KeSaveExtendedProcessorState / KeRestoreExtendedProcessorState.
FLAGS = \
	--target=$(TARGET) \
	-g \
	-fms-kernel \
	-fms-extensions \
	-fms-compatibility \
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

$(DRIVER): $(SOURCES)
	$(NEVERC) $(FLAGS) -o $@ $^

clean:
	rm -f $(DRIVER)

.PHONY: all clean
