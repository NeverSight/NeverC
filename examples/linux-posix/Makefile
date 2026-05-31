NEVERC ?= ../../build-neverc/bin/neverc

TARGET ?= x86_64-linux-gnu
OUTPUT  = posix-demo

FLAGS = \
	--target=$(TARGET) \
	-O2 \
	-Wall \
	-lpthread

all: $(OUTPUT)

$(OUTPUT): main.c
	$(NEVERC) $(FLAGS) -o $@ $^

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
