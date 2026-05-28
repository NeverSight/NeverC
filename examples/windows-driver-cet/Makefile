NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
DRIVER  = CetDriver.sys
SOURCES = driver.c

# -fcf-protection=return: generate CET Shadow Stack compatible code (KCET).
#     Windows kernel uses Shadow Stack only (no IBT; CFG is used instead).
# --cetcompat: mark PE as CET Shadow Stack compatible
#     (IMAGE_DLL_CHARACTERISTICS_EX_CET_COMPAT in debug directory).
# -g: emit DWARF debug info; remove for release builds.
FLAGS = \
	--target=$(TARGET) \
	-g \
	-fcf-protection=return \
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
	-Xlinker --cetcompat \
	-lntoskrnl -lhal

all: $(DRIVER)

$(DRIVER): $(SOURCES)
	$(NEVERC) $(FLAGS) -o $@ $^

clean:
	rm -f $(DRIVER)

.PHONY: all clean
