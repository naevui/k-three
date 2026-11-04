# SPDX-License-Identifier: GPL-2.0
#
# arch/k3/Makefile - Linux Architecture Makefile for K-Three (Kord-Three)
#

KBUILD_DEFCONFIG := defconfig

# Architecture compiler flags
KBUILD_CFLAGS += -pipe -std=c11 -Wall -Wextra -Wpedantic
KBUILD_CFLAGS += -D__K3__ -D__linux__
KBUILD_AFLAGS += -D__K3__

# Memory layout and stack conventions
KBUILD_CFLAGS += -fno-builtin -ffreestanding

# Kernel entry point and core object directories
head-y := arch/k3/kernel/entry.o
core-y += arch/k3/kernel/ arch/k3/fs/

# Linker script
KBUILD_LDS := arch/k3/kernel/vmlinux.lds

all: vmlinux

boot := arch/k3/boot

vmlinux: $(head-y) $(core-y)

PHONY += clean
clean:
	$(Q)$(MAKE) $(clean)=$(boot)
