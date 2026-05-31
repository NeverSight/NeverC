NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
OUTPUT  = example.dll

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-shared \
	-Xlinker --entry=DllMain \
	-Xlinker --subsystem=windows \
	-lkernel32 -luser32

all: $(OUTPUT)

$(OUTPUT): dllmain.c
	$(NEVERC) $(FLAGS) -o $@ $^

clean:
	rm -f $(OUTPUT) $(OUTPUT:.dll=.lib)

.PHONY: all clean
