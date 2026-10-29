/*
 * arch/k3/boot.c
 *
 * Boot loader and kernel entry simulator for K-Three Ternary Linux.
 * Boots the kernel, sets up hardware vectors, and starts /sbin/init (PID 1).
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "vm.h"
#include "image.h"
#include "syscall.h"
#include "arch/k3/include/asm/processor.h"

extern void k3_kernel_banner(void);
extern void k3_setup_arch(k3_vm *vm, uint16_t trap_vector_addr);
extern void k3_init_tasks(void);

int main(int argc, char **argv)
{
    const char *init_image = "build/userspace/init.k3";
    if (argc > 1) {
        init_image = argv[1];
    }

    /* 1. Print kernel boot dmesg banner */
    k3_kernel_banner();

    /* 2. Initialize tasks and scheduler structures */
    k3_init_tasks();

    /* 3. Initialize K-Three VM Hardware */
    k3_vm vm;
    k3_vm_callbacks callbacks = {
        .user = NULL,
        .on_step = NULL,
        .on_stop = NULL,
        .on_syscall = k3_host_syscall,
    };
    k3_vm_init(&vm, &callbacks);

    /* 4. Setup kernel architecture hooks & trap vector */
    k3_setup_arch(&vm, 0x0000);

    /* 5. Load /sbin/init image into user memory */
    bt_word words[K3_VM_MEMORY_WORDS];
    size_t count = 0;
    if (!k3_image_read(init_image, words, K3_VM_MEMORY_WORDS, &count)) {
        fprintf(stderr, "\033[1;31m[kernel panic] VFS: Unable to mount root fs or open %s\033[0m\n", init_image);
        return 1;
    }

    if (!k3_vm_load(&vm, words, count, 0)) {
        fprintf(stderr, "\033[1;31m[kernel panic] Failed to load %s into memory\033[0m\n", init_image);
        return 1;
    }

    /* 6. Jump into user mode (PID 1) */
    k3_vm_set_mode(&vm, K3_MODE_USER);

    uint64_t executed = 0;
    k3_vm_status status = k3_vm_run(&vm, 10000000, &executed);

    if (status == K3_VM_HALTED) {
        printf("\n\033[1;36m[kernel] System safely halted.\033[0m\n");
    } else {
        printf("\n\033[1;31m[kernel panic] Process 1 died with status %d\033[0m\n", status);
    }

    return (status == K3_VM_HALTED) ? 0 : 1;
}
