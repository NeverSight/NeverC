# Windows Kernel Driver — minimal WDM driver cross-compiled from any host
# Usage: neverc make

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
DRIVER  = ExampleDriver.sys

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

SRCS = driver.c
OBJS = $(SRCS:.c=.o)
DEPS = $(OBJS:.o=.d)

all: $(DRIVER)

# Link
$(DRIVER): $(OBJS)
	$(NEVERC) $(FLAGS) -o $@ $^

# Compile (generates .d dependency files for incremental builds)
%.o: %.c
	$(NEVERC) $(FLAGS) -MMD -c $< -o $@

clean:
	rm -f $(DRIVER) $(OBJS) $(DEPS)

# Auto-generated header dependencies
-include $(DEPS)

.PHONY: all clean
