# XQuest 32X Makefile
# Uses the marsdev sh-elf / m68k-elf toolchains (or Chilly's devkit).

MARSDEV ?= /home/user/tools/mars
CHILLY  ?= /home/user/tools/chillydk/opt/toolchains/sega

SHPREFIX = $(MARSDEV)/sh-elf/bin/sh-elf-
MDPREFIX = $(MARSDEV)/m68k-elf/bin/m68k-elf-

SHCC   = $(SHPREFIX)gcc
SHAS   = $(SHPREFIX)as
SHLD   = $(SHPREFIX)ld
SHOC   = $(SHPREFIX)objcopy
MDAS   = $(MDPREFIX)as
MDLD   = $(MDPREFIX)ld

BUILD  = build
GEN    = gen
SRC    = src

CCFLAGS  = -m2 -mb -O2 -Wall -fomit-frame-pointer -ffreestanding \
           -fno-builtin -nostdlib -I$(SRC) -I$(GEN)
LDFLAGS  = -T $(SRC)/mars.ld -Wl,-Map=$(BUILD)/output.map -nostdlib
LIBS     = -lgcc

OBJS = $(BUILD)/sh2_crt0.o \
       $(BUILD)/hw32x.o \
       $(BUILD)/render.o \
       $(BUILD)/sound.o \
       $(BUILD)/game.o \
       $(BUILD)/libc_min.o \
       $(BUILD)/assets.o

all: $(BUILD)/xquest32x.32x
	cp $(BUILD)/xquest32x.32x $(CURDIR)/xquest32x.32x
	@echo "ROM: $(CURDIR)/xquest32x.32x"

$(GEN)/assets.c $(GEN)/assets.h: tools/convert_assets.py \
        assets/xquest.gfx assets/xquest.enm assets/xquest.fnt \
        assets/xquest2.fnt assets/xquest.snd assets/palette.inc \
        assets/titlemap.inc assets/title.pbm assets/xqvars.pas
	python3 tools/convert_assets.py assets $(GEN)

$(BUILD)/xquest32x.32x: $(BUILD)/xquest.elf
	$(SHOC) -O binary $< $(BUILD)/temp.bin
	dd if=$(BUILD)/temp.bin of=$@ bs=64K conv=sync status=none
	python3 tools/fix_checksum.py $@

$(BUILD)/xquest.elf: $(OBJS)
	$(SHCC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

# The SH2 crt0 pulls in the two 68000 binaries.
$(BUILD)/sh2_crt0.o: $(SRC)/sh2_crt0.s $(BUILD)/m68k_crt0.bin $(BUILD)/m68k_crt1.bin
	cp $(BUILD)/m68k_crt0.bin $(BUILD)/m68k_crt1.bin .
	$(SHAS) --small -o $@ $<
	rm -f m68k_crt0.bin m68k_crt1.bin

$(BUILD)/m68k_crt0.bin: $(SRC)/m68k_crt0.s | $(BUILD)
	$(MDAS) -m68000 --register-prefix-optional -o $(BUILD)/m68k_crt0.o $<
	$(MDLD) -T $(SRC)/md.ld --oformat binary -o $@ $(BUILD)/m68k_crt0.o

$(BUILD)/m68k_crt1.bin: $(SRC)/m68k_crt1.s | $(BUILD)
	$(MDAS) -m68000 --register-prefix-optional -o $(BUILD)/m68k_crt1.o $<
	$(MDLD) -T $(SRC)/md.ld --oformat binary -o $@ $(BUILD)/m68k_crt1.o

$(BUILD)/%.o: $(SRC)/%.c $(GEN)/assets.h | $(BUILD)
	$(SHCC) $(CCFLAGS) -c $< -o $@

$(BUILD)/assets.o: $(GEN)/assets.c $(GEN)/assets.h | $(BUILD)
	$(SHCC) $(CCFLAGS) -O1 -c $< -o $@

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) $(GEN)

.PHONY: all clean
