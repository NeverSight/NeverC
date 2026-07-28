# Windows Kernel Driver — minimal WDM driver cross-compiled from any host
# Usage: neverc make               (x64, the default)
#        neverc make ARCH=arm64
#        neverc make all-arch      (both)
#        neverc make TESTSIGN=1    (attach an Authenticode test signature)

NEVERC ?= ../../build-neverc/bin/neverc

ARCH   ?= x64

ifeq ($(ARCH),x64)
TARGET = x86_64-pc-windows-msvc
else ifeq ($(ARCH),arm64)
TARGET = aarch64-pc-windows-msvc
else
$(error ARCH must be x64 or arm64, got '$(ARCH)')
endif

DRIVER  = ExampleDriver-$(ARCH).sys

SRCS    = driver.c
HEADERS = config.h

# -fms-kernel selects the WDK headers and import libraries for the target
# architecture, and defines the environment they expect (_AMD64_ / _ARM64_ and
# _KERNEL_MODE), so none of that has to be restated here.
# -g: emit DWARF debug info; remove for release builds
FLAGS = \
	--target=$(TARGET) \
	-g \
	-fms-kernel \
	-D_CRT_SECURE_NO_WARNINGS \
	-Wall \
	-nostdlib \
	-shared \
	-Xlinker --entry=DriverEntry \
	-Xlinker --subsystem=native \
	-Xlinker --nodefaultlib \
	-lntoskrnl -lhal

# TESTSIGN=1 signs the image with the compiler's built-in test identity, which
# is what lets it load on a machine with test signing on.  See the README for
# the one-time setup that machine needs.
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
	rm -f ExampleDriver-x64.sys ExampleDriver-arm64.sys ExampleDriver.sys

.PHONY: all all-arch clean
