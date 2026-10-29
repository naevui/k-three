/*
 * arch/k3/kernel/setup.c
 *
 * Architecture initialization, hardware discovery and kernel boot sequence.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#include "vm.h"
#include "arch/k3/include/asm/page.h"
#include "arch/k3/include/asm/processor.h"
#include "arch/k3/include/asm/irq.h"

void k3_kernel_banner(void)
{
    printf("\033[1;36m[    0.000000] Linux version 0.1-k3 (root@k3-build) (k3-elf-gcc) #1 PREEMPT\033[0m\n");
    printf("[    0.000000] Machine model: K-Three 9-Trit Balanced Ternary Processor\n");
    printf("[    0.000000] CPU features: 9-trit ALU, symmetric math, [-9841..+9841]\n");
    printf("[    0.000000] Memory: %d words available (%d pages of %d words, NOMMU)\n",
           K3_VM_MEMORY_WORDS, K3_TOTAL_PAGES, K3_PAGE_SIZE);
    printf("[    0.000000] Zone PFN: Kernel [0..%d], Userspace [%d..%d]\n",
           K3_PAGE_SIZE * 2 - 1, K3_PAGE_SIZE * 2, K3_VM_MAX_ADDRESS);
    printf("[    0.000000] Trap Vector registered at address 0x%04x\n", 0);
    printf("[    0.000000] Calibrating delay loop... 42.00 BogoMIPS\n");
    printf("[    0.000000] devtmpfs: initialized\n");
    printf("[    0.000000] VFS: Mounted root (ternary-romfs filesystem) readonly.\n");
    printf("\033[1;32m[    0.000000] Freeing unused kernel memory... Done\033[0m\n");
    printf("\033[1;33m[    0.000000] Run /sbin/init as init process (pid 1)\033[0m\n");
    fflush(stdout);
}

void k3_setup_arch(k3_vm *vm, uint16_t trap_vector_addr)
{
    k3_vm_set_trap_vector(vm, (int16_t)trap_vector_addr);
    k3_vm_set_mode(vm, K3_MODE_KERNEL);
}
