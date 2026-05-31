NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
OUTPUT  = example.exe

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-Xlinker --subsystem=console \
	-lkernel32 -luser32 -lmsvcrt

all: $(OUTPUT)

$(OUTPUT): main.c
	$(NEVERC) $(FLAGS) -o $@ $^

clean:
	rm -f $(OUTPUT) $(OUTPUT:.exe=.lib)

.PHONY: all clean
