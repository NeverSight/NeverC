# Windows Kernel Driver + CET Shadow Stack
# Usage: neverc make               (x64, the only architecture here)
#        neverc make all-arch      (same thing — x64 is the only one)
#        neverc make TESTSIGN=1    (attach an Authenticode test signature)
#
# -fcf-protection=return: CET Shadow Stack compatible code (KCET)
# --cetcompat: mark PE as CET Shadow Stack compatible

NEVERC ?= ../../build-neverc/bin/neverc

ARCH   ?= x64

# CET is an x86 feature and the compiler rejects -fcf-protection=return on other
# targets, so this example is x64-only.  ARCH and all-arch still exist so the
# driver examples take the same commands, and so asking for ARM64 explains
# itself here instead of failing further in.
ifeq ($(ARCH),x64)
TARGET = x86_64-pc-windows-msvc
else
$(error CET is x64-only, got ARCH='$(ARCH)'. ARM64 protects the same edges with pointer authentication and branch target identification, not CET)
endif

DRIVER  = CetDriver.sys

SRCS    = driver.c
HEADERS = config.h

# -fms-kernel selects the WDK headers and import libraries for the target
# architecture, and defines the environment they expect (_AMD64_ and
# _KERNEL_MODE), so none of that has to be restated here.
# --driver marks the image as kernel-mode: it makes the code and data sections
# non-paged (a driver touching paged memory at raised IRQL bugchecks), moves the
# import tables into the discardable INIT section, and fills in the PE checksum
# the kernel loader verifies.
FLAGS = \
	--target=$(TARGET) \
	-g \
	-fcf-protection=return \
	-fms-kernel \
	-D_CRT_SECURE_NO_WARNINGS \
	-Wall \
	-nostdlib \
	-shared \
	-Xlinker --driver \
	-Xlinker --entry=DriverEntry \
	-Xlinker --subsystem=native \
	-Xlinker --nodefaultlib \
	-Xlinker --cetcompat \
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

clean:
	rm -f $(DRIVER)

.PHONY: all all-arch clean
