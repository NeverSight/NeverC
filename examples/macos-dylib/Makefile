# macOS Dynamic Library — cross-compile .dylib from any host
# Usage: neverc make [TARGET=arm64-apple-macos]

NEVERC ?= ../../build-neverc/bin/neverc

TARGET ?= arm64-apple-macos
OUTPUT  = libneverc.dylib

SRCS    = lib.c
HEADERS = config.h

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-dynamiclib

all: $(OUTPUT)

$(OUTPUT): $(SRCS) $(HEADERS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
