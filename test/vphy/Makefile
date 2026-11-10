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

# The PHY files the harness runs, and what they call of main.c.
PHY       := phy_common.c phy_g.c lo.c tables.c wa.c
MAIN_PART := b43_ram_write b43_shm_control_word b43_shm_read32 b43_shm_read16 \
             b43_shm_write32 b43_shm_write16 b43_hf_read b43_hf_write \
             b43_dummy_transmission b43_phy_name b43_phy_versioning

OBJS := $(PHY:%.c=build/%.o) build/driver.o build/host.o

.PHONY: all clean
all: b43phy

build/main_part.c: $(B43)/main.c extract.py
	@mkdir -p build
	python3 extract.py $< $(MAIN_PART) > $@

build/%.o: $(B43)/%.c
	@mkdir -p build
	$(CC) -c $< -o $@ $(KFLAGS)

build/driver.o: driver.c build/main_part.c
	$(CC) -c driver.c -o $@ $(KFLAGS) -Ibuild

build/host.o: host.c
	@mkdir -p build
	$(CC) -c host.c -o $@ -O2 -Wall -std=gnu11

b43phy: $(OBJS)
	$(CC) -o $@ $(OBJS)

clean:
	rm -rf build b43phy
