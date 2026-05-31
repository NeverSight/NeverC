NEVERC ?= ../../build-neverc/bin/neverc

TARGET ?= x86_64-linux-gnu
OUTPUT  = math-demo

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-lm -lz

all: $(OUTPUT)

$(OUTPUT): main.c
	$(NEVERC) $(FLAGS) -o $@ $^

clean:
	rm -f $(OUTPUT)

.PHONY: all clean
