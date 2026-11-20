CROSS ?= arm-none-eabi-
CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size
CFLAGS := -mcpu=cortex-m7 -mthumb -mfloat-abi=soft -std=c11 -Os -g3 -ffreestanding \
          -ffunction-sections -fdata-sections -Wall -Wextra -Werror
LDFLAGS := -nostdlib -Wl,--gc-sections -T firmware/layout.ld
COMMON := firmware/startup.S firmware/platform.c firmware/journal.c firmware/integrity.c firmware/runtime.c
HEADERS := $(wildcard firmware/*.h) firmware/layout.ld

.PHONY: all clean test
all: build/boot.bin build/app-a.bin build/app-b.bin build/app-b-fail.bin

build:
	mkdir -p $@

build/boot.elf: $(COMMON) firmware/boot.c firmware/image.c $(HEADERS) | build
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=image_origin=0x08000000,--defsym=image_capacity=0x20000 $(filter-out $(HEADERS),$^) -lgcc -o $@

build/app-a.elf: ORIGIN=0x08040200
build/app-a.elf: SLOT=0
build/app-b.elf build/app-b-fail.elf: ORIGIN=0x08100200
build/app-b.elf build/app-b-fail.elf: SLOT=1
build/app-b-fail.elf: FAIL=1
build/app-a.elf build/app-b.elf build/app-b-fail.elf: $(COMMON) firmware/demo.c $(HEADERS) | build
	$(CC) $(CFLAGS) $(LDFLAGS) -Wl,--defsym=image_origin=$(ORIGIN),--defsym=image_capacity=0xbfe00 -DAPP_SLOT=$(SLOT) -DDEMO_FAIL=$(or $(FAIL),0) $(filter-out $(HEADERS),$^) -lgcc -o $@

build/%.bin: build/%.elf
	$(OBJCOPY) -O binary $< $@
	$(SIZE) $<

test:
	python3 -m unittest discover -s tests -v

clean:
	rm -rf build

HOST_CC ?= cc
.PHONY: test-native
test-native: build/test-firmware
	./build/test-firmware

build/test-firmware: tests/test_firmware.c firmware/journal.c firmware/integrity.c firmware/image.c $(HEADERS) | build
	$(HOST_CC) -std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Ifirmware $(filter-out $(HEADERS),$^) -o $@
