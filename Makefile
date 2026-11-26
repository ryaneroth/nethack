CL65 ?= cl65

# nethack.bin is the ROM image for $A000; nethack-ram.bin is the RAM image
# for $2000 (copy it to the SD card as NETHACK.BIN).  Both come from one
# link and carry the same build stamp, so the ROM can tell a matching RAM
# image from a stale one.
TARGET = nethack.bin
RAMIMG = nethack-ram.bin
BUILD_ID := $(shell expr $$(date +%s) % 65000 + 1)

.PHONY: all clean ptp

all: $(TARGET)

$(TARGET): kim1_io.s nethack_sd.s nethack.c kim1rom.cfg vendor/sdcard6502/src/libsd.s vendor/sdcard6502/src/libfat32.s
	$(CL65) -t none -C kim1rom.cfg -Osir --static-locals -DBUILD_ID=$(BUILD_ID)u -m nethack.map -Ln nethack.lbl -o $@ kim1_io.s nethack_sd.s nethack.c

# MOS Technology paper-tape images of both halves, for loading over serial.
ptp: $(TARGET)
	srec_cat $(TARGET) -binary -offset 0xa000 -o nethack.ptp -MOS_Technologies
	srec_cat $(RAMIMG) -binary -offset 0x2000 -o nethack-ram.ptp -MOS_Technologies

clean:
	rm -f $(TARGET) $(RAMIMG) nethack.lbl nethack.map nethack.ptp nethack-ram.ptp *.o
