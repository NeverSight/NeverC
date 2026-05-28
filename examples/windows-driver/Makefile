NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
DRIVER  = ExampleDriver.sys
SOURCES = driver.c

CFLAGS = \
	--target=$(TARGET) \
	-fms-kernel \
	-fms-extensions \
	-fms-compatibility \
	-D_AMD64_ \
	-DNTDDI_VERSION=0x06010000 \
	-D_WIN32_WINNT=0x0601 \
	-Wall

LDFLAGS = \
	--target=$(TARGET) \
	-fms-kernel \
	-nostdlib \
	-shared \
	-Wl,--entry:DriverEntry \
	-Wl,--subsystem:native

LIBS = -lntoskrnl -lhal -lBufferOverflowK

all: $(DRIVER)

$(DRIVER): $(SOURCES)
	$(NEVERC) $(CFLAGS) $(LDFLAGS) $(LIBS) -o $@ $^

clean:
	rm -f $(DRIVER)

.PHONY: all clean
