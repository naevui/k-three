/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/k3/kernel/vmlinux.lds.S
 *
 * Linker script for the K-Three Balanced Ternary Linux Kernel.
 */

#define K3_RAM_START        0x0000
#define K3_KERNEL_PAGES     2
#define K3_PAGE_SIZE        729
#define K3_USER_START       (K3_KERNEL_PAGES * K3_PAGE_SIZE) /* 1458 */
#define K3_RAM_END          6560

OUTPUT_ARCH(k3)
ENTRY(start)

SECTIONS
{
    /* Kernel code starts at physical address 0 */
    . = K3_RAM_START;
    _stext = .;
    __text_start = .;

    .text : {
        *(.head.text)
        *(.text)
        *(.text.*)
    }
    _etext = .;

    . = ALIGN(K3_PAGE_SIZE);
    __rodata_start = .;
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    __rodata_end = .;

    /* Kernel Data & BSS in Page 1 (729..1457) */
    . = K3_PAGE_SIZE;
    __data_start = .;
    .data : {
        *(.data)
        *(.data.*)
    }
    __data_end = .;

    . = ALIGN(4);
    __bss_start = .;
    .bss : {
        *(.bss)
        *(.bss.*)
    }
    __bss_end = .;
    _end = .;

    /* User Space Boundary begins at Page 2 (Address 1458) */
    . = K3_USER_START;
    __user_start = .;
}
