# Linux Static — fully static binary, zero runtime dependencies
# Usage: neverc make [TARGET=aarch64-linux-gnu]

NEVERC ?= ../../build-neverc/bin/neverc

TARGET ?= x86_64-linux-gnu
OUTPUT  = static-demo

SRCS    = main.c
HEADERS = config.h

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-static \
	-lm

all: $(OUTPUT)

$(OUTPUT): $(SRCS) $(HEADERS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
