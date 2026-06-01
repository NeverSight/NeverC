# Windows Kernel Driver + CET Shadow Stack
# Usage: neverc make
#
# -fcf-protection=return: CET Shadow Stack compatible code (KCET)
# --cetcompat: mark PE as CET Shadow Stack compatible

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
DRIVER  = CetDriver.sys

FLAGS = \
	--target=$(TARGET) \
	-g \
	-fcf-protection=return \
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
	-Xlinker --cetcompat \
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
