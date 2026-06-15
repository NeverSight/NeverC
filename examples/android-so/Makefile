# Android Shared Library — ARM64 .so for rooted devices
# Usage: neverc make

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
OUTPUT  = libneverc.so

SRCS    = lib.c
HEADERS = config.h

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-shared \
	-fPIC \
	-ldl

all: $(OUTPUT)

$(OUTPUT): $(SRCS) $(HEADERS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

push: $(OUTPUT)
	adb push $(OUTPUT) /data/local/tests/
	adb shell chmod 755 /data/local/tests/$(OUTPUT)

clean:
	rm -f $(OUTPUT)

.PHONY: all push clean
