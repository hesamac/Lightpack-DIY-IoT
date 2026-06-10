# =============================================================================
# Makefile — Lightpack AT90USB162 firmware
#
# Requirements
# ------------
#   macOS (Apple Silicon or Intel):
#       brew tap osx-cross/avr
#       brew install avr-gcc avrdude
#
#   LUFA (Lightweight USB Framework for AVRs):
#       git clone https://github.com/abcminiuser/lufa.git lufa-upstream
#   LUFA_PATH must point to the "LUFA/" subdirectory *inside* the cloned repo,
#   i.e. the directory that contains Drivers/, Build/, Common/ etc.:
#       make LUFA_PATH=lufa-upstream/LUFA          (if cloned next to this Makefile)
#       make LUFA_PATH=/absolute/path/to/lufa/LUFA (system-wide install)
#
# Build
# -----
#   make              → build lightpack.hex
#   make flash        → build + flash via DFU (press HWB button first)
#   make clean        → remove build artefacts
#
# =============================================================================

# ---- Target MCU ----------------------------------------------------------
MCU     := at90usb162
F_CPU   := 16000000UL
F_USB   := 16000000UL
TARGET  := lightpack

# ---- LUFA path -----------------------------------------------------------
# Point this at the "LUFA/" subdirectory inside the git repo clone, i.e.
# the directory that contains Drivers/, Build/, Common/ etc.
#
#   git clone https://github.com/abcminiuser/lufa.git lufa-upstream
#   make LUFA_PATH=lufa-upstream/LUFA
#
LUFA_PATH ?= lufa-upstream/LUFA

# ---- Toolchain -----------------------------------------------------------
CC      := avr-gcc
OBJCOPY := avr-objcopy
OBJDUMP := avr-objdump
SIZE    := avr-size

# ---- Source files --------------------------------------------------------
SRC  = src/main.c
SRC += src/Descriptors.c
SRC += src/lightpack.c
SRC += src/dm631.c

# LUFA core (device-mode only — matches LUFA_OPTS below)
LUFA_SRC  = $(LUFA_PATH)/Drivers/USB/Core/AVR8/Device_AVR8.c
LUFA_SRC += $(LUFA_PATH)/Drivers/USB/Core/AVR8/Endpoint_AVR8.c
LUFA_SRC += $(LUFA_PATH)/Drivers/USB/Core/AVR8/EndpointStream_AVR8.c
LUFA_SRC += $(LUFA_PATH)/Drivers/USB/Core/AVR8/USBController_AVR8.c
LUFA_SRC += $(LUFA_PATH)/Drivers/USB/Core/AVR8/USBInterrupt_AVR8.c
LUFA_SRC += $(LUFA_PATH)/Drivers/USB/Core/ConfigDescriptors.c
LUFA_SRC += $(LUFA_PATH)/Drivers/USB/Core/DeviceStandardReq.c
LUFA_SRC += $(LUFA_PATH)/Drivers/USB/Core/Events.c
LUFA_SRC += $(LUFA_PATH)/Drivers/USB/Core/USBTask.c

ALL_SRC := $(SRC) $(LUFA_SRC)

# ---- LUFA compile-time options -------------------------------------------
LUFA_OPTS  = -DF_USB=$(F_USB)
LUFA_OPTS += -DARCH=ARCH_AVR8
LUFA_OPTS += -DBOARD=BOARD_NONE
LUFA_OPTS += -DUSE_LUFA_CONFIG_HEADER

# ---- Compiler flags -------------------------------------------------------
CFLAGS  = -mmcu=$(MCU)
CFLAGS += -DF_CPU=$(F_CPU)
CFLAGS += -Os                          # optimise for size (8 KB flash limit)
CFLAGS += -std=gnu99
CFLAGS += -Wall -Wextra -Wno-unused-parameter
CFLAGS += -ffunction-sections          # allow dead-code stripping by linker
CFLAGS += -fdata-sections
CFLAGS += -I./src                      # find LUFAConfig.h in src/
CFLAGS += -I$(LUFA_PATH)/..            # LUFA headers: #include "LUFA/Drivers/..."
CFLAGS += $(LUFA_OPTS)

# ---- Linker flags ---------------------------------------------------------
LDFLAGS  = -mmcu=$(MCU)
LDFLAGS += -Wl,--gc-sections           # strip unused sections

# ---- Build rules ----------------------------------------------------------
OBJ := $(ALL_SRC:.c=.o)

.PHONY: all clean flash size

all: $(TARGET).hex size

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -j .text -j .data -O ihex $< $@

$(TARGET).eep: $(TARGET).elf
	$(OBJCOPY) -j .eeprom --change-section-lma .eeprom=0 -O ihex $< $@ || true

%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

size: $(TARGET).elf
	$(SIZE) --format=avr --mcu=$(MCU) $<

clean:
	rm -f $(OBJ) $(TARGET).elf $(TARGET).hex $(TARGET).eep

# ---- Flash via DFU-programmer ---------------------------------------------
# 1. Hold the HWB (hardware boot) button on the Lightpack board, then press
#    and release RESET (or plug USB while holding HWB) — the MCU enters the
#    AT90USB162 built-in DFU bootloader.
# 2. Run:  make flash
flash: $(TARGET).hex
	dfu-programmer at90usb162 erase  --force
	dfu-programmer at90usb162 flash  $(TARGET).hex
	dfu-programmer at90usb162 launch
