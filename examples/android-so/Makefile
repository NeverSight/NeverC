NEVERC ?= ../../build-neverc/bin/neverc

TARGET  = aarch64-linux-android
OUTPUT  = libneverc.so

FLAGS = \
	--target=$(TARGET) \
	-Wall \
	-shared \
	-fPIC \
	-ldl

all: $(OUTPUT)

$(OUTPUT): lib.c
	$(NEVERC) $(FLAGS) -o $@ $^

push: $(OUTPUT)
	adb push $(OUTPUT) /data/local/tmp/
	adb shell chmod 755 /data/local/tmp/$(OUTPUT)

clean:
	rm -f $(OUTPUT)

.PHONY: all push clean
