NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
OUTPUT  = android-elf

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-fPIE \
	-lm \
	-ldl \
	-llog

all: $(OUTPUT)

$(OUTPUT): main.c
	$(NEVERC) $(FLAGS) -o $@ $^

push: $(OUTPUT)
	adb push $(OUTPUT) /data/local/tmp/
	adb shell chmod 755 /data/local/tmp/$(OUTPUT)

run: push
	adb shell /data/local/tmp/$(OUTPUT)

clean:
	rm -f $(OUTPUT)

.PHONY: all push run clean
