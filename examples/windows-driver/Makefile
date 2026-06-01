# Windows Kernel Driver — minimal WDM driver cross-compiled from any host
# Usage: neverc make

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
DRIVER  = ExampleDriver.sys

SRCS    = driver.c
HEADERS = config.h

# -g: emit DWARF debug info; remove for release builds
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
