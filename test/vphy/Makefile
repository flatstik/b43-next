# SPDX-License-Identifier: GPL-2.0-only
#
# b43-next's PHY code, unmodified, against the headers of the kernel the
# module is built for, linked into a user program that reaches the chip
# through b43-tools' interpreter/vchipd.py.
#
# -O2 and -mno-red-zone are what the headers need: without the first they do
# not compile, without the second their inline asm calls overwrite the locals
# of leaf functions.

KVER ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KVER)/build
ARCH ?= x86
TOP  := ../..
B43  := $(TOP)/b43-next

KFLAGS := -nostdinc -isystem $(shell $(CC) -print-file-name=include) \
          -I$(B43) -I$(TOP)/include \
          -I$(KSRC)/arch/$(ARCH)/include -I$(KSRC)/arch/$(ARCH)/include/generated \
          -I$(KSRC)/include -I$(KSRC)/arch/$(ARCH)/include/uapi \
          -I$(KSRC)/arch/$(ARCH)/include/generated/uapi -I$(KSRC)/include/uapi \
          -I$(KSRC)/include/generated/uapi \
          -include $(KSRC)/include/linux/kconfig.h \
          -include $(KSRC)/include/linux/compiler_types.h \
          -include $(TOP)/compat/compat.h \
          -D__KERNEL__ -DMODULE -DKBUILD_MODNAME=\"b43_next\" \
          -DCONFIG_B43_NEXT_SSB=1 -DCONFIG_B43_NEXT_PHY_G=1 \
          -O2 -fno-strict-aliasing -fno-common -std=gnu11 -mno-red-zone \
          -Wno-address-of-packed-member -Werror=implicit-function-declaration

# Every function of the PHY files marks its entry and exit in the trace,
# except the register accessors, whose accesses are in it already.
ACCESSORS := b43_phy_read b43_phy_write b43_phy_mask b43_phy_set b43_phy_maskset \
             b43_radio_read b43_radio_write b43_radio_mask b43_radio_set b43_radio_maskset \
             b43_gphy_op_read b43_gphy_op_write b43_gphy_op_radio_read b43_gphy_op_radio_write
comma := ,
space := $(subst ,, )
MARKS := -finstrument-functions \
         -finstrument-functions-exclude-function-list=$(subst $(space),$(comma),$(ACCESSORS))

# The PHY files the harness runs, and what they call of main.c.
PHY       := phy_common.c phy_g.c lo.c tables.c wa.c
MAIN_PART := b43_ram_write b43_shm_control_word b43_shm_read32 b43_shm_read16 \
             b43_shm_write32 b43_shm_write16 b43_hf_read b43_hf_write \
             b43_dummy_transmission b43_phy_name b43_phy_versioning

OBJS := $(PHY:%.c=build/%.o) build/main_part.o build/driver.o build/host.o

.PHONY: all preprocessed clean
all: b43phy

# The PHY files as the compiler sees them, for fields.py.
preprocessed: $(PHY:%.c=build/%.i) build/main_part.i

build/main_part.i: build/main_part.c
	$(CC) -E $< -o $@ $(KFLAGS)

build/%.i: $(B43)/%.c
	@mkdir -p build
	$(CC) -E $< -o $@ $(KFLAGS)

build/main_part.c: $(B43)/main.c extract.py
	@mkdir -p build
	python3 extract.py $< $(MAIN_PART) > $@

build/%.o: $(B43)/%.c
	@mkdir -p build
	$(CC) -c $< -o $@ $(KFLAGS) $(MARKS)

build/main_part.o: build/main_part.c
	$(CC) -c $< -o $@ $(KFLAGS) $(MARKS)

build/driver.o: driver.c
	@mkdir -p build
	$(CC) -c driver.c -o $@ $(KFLAGS)

build/host.o: host.c
	@mkdir -p build
	$(CC) -c host.c -o $@ -O2 -Wall -std=gnu11

b43phy: $(OBJS)
	$(CC) -no-pie -o $@ $(OBJS)

clean:
	rm -rf build b43phy
