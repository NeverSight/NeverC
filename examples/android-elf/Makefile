# Android Native ELF — ARM64 binary for rooted devices
# Usage: neverc make
#        neverc make run    (build + push via adb + execute)

NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
OUTPUT  = android-elf

SRCS    = main.c
HEADERS = config.h

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-fPIE \
	-lm \
	-ldl \
	-llog

all: $(OUTPUT)

$(OUTPUT): $(SRCS) $(HEADERS)
	$(NEVERC) $(FLAGS) -o $@ $(SRCS)

push: $(OUTPUT)
	adb push $(OUTPUT) /data/local/tmp/
	adb shell chmod 755 /data/local/tmp/$(OUTPUT)

run: push
	adb shell /data/local/tmp/$(OUTPUT)

clean:
	rm -f $(OUTPUT)

.PHONY: all push run clean
