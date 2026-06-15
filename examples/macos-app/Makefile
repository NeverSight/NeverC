# macOS Application — cross-compile Mach-O executable from any host
# Usage: neverc make [TARGET=arm64-apple-macos]

NEVERC ?= ../../build-neverc/bin/neverc

TARGET ?= arm64-apple-macos
OUTPUT  = macos-app

SRCS    = main.c
HEADERS = config.h

FLAGS = \
	--target=$(TARGET) \
	-Wall

all: $(OUTPUT)

$(OUTPUT): $(SRCS) $(HEADERS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
