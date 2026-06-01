# Linux POSIX — pthreads, mmap, pipe, signal handling
# Usage: neverc make [TARGET=aarch64-linux-gnu]

NEVERC ?= ../../build-neverc/bin/neverc

TARGET ?= x86_64-linux-gnu
OUTPUT  = posix-demo

SRCS    = main.c
HEADERS = config.h

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-lpthread

all: $(OUTPUT)

$(OUTPUT): $(SRCS) $(HEADERS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
