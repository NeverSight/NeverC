# Windows Ring3 DLL — user-mode dynamic library
# Usage: neverc make

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
OUTPUT  = example.dll

SRCS    = dllmain.c
HEADERS = config.h

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-shared \
	-Xlinker --entry=DllMain \
	-Xlinker --subsystem=windows \
	-lkernel32 -luser32

all: $(OUTPUT)

$(OUTPUT): $(SRCS) $(HEADERS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT) $(OUTPUT:.dll=.lib)

.PHONY: all clean
