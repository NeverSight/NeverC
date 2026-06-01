# Windows Ring3 EXE — user-mode console application
# Usage: neverc make

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = x86_64-pc-windows-msvc
OUTPUT  = example.exe

SRCS    = main.c
HEADERS = config.h

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-Xlinker --subsystem=console \
	-lkernel32 -luser32 -lmsvcrt

all: $(OUTPUT)

$(OUTPUT): $(SRCS) $(HEADERS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT) $(OUTPUT:.exe=.lib)

.PHONY: all clean
