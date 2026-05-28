NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
DRIVER  = ExampleDriver.sys
SOURCES = driver.c

# -g: emit DWARF debug info (PE + DWARF); useful with WinDbg / llvm-dwarfdump.
#     For release builds, remove -g to reduce binary size.
FLAGS = \
	--target=$(TARGET) \
	-g \
	-fms-kernel \
	-fms-extensions \
	-fms-compatibility \
	-D_AMD64_ \
	-DNTDDI_VERSION=0x06010000 \
	-D_WIN32_WINNT=0x0601 \
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
