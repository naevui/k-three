#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "syscall.h"
#include "ternary.h"
#include "vm.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(expr) do { \
    g_checks++; \
    if (!(expr)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

static void helper_set_reg(k3_vm *vm, uint8_t r, int64_t val)
{
    bt_word w;
    bt_word_from_i64(val, &w);
    k3_vm_set_reg(vm, r, &w);
}

static int64_t helper_get_reg(const k3_vm *vm, uint8_t r)
{
    bt_word w;
    int64_t val = 0;
    k3_vm_get_reg(vm, r, &w);
    bt_word_to_i64(&w, &val);
    return val;
}

static void helper_write_mem_str(k3_vm *vm, uint16_t addr, const char *s)
{
    while (*s) {
        bt_word w;
        bt_word_from_i64((unsigned char)*s, &w);
        k3_vm_set_mem(vm, addr++, &w);
        s++;
    }
    bt_word zero;
    bt_word_zero(&zero);
    k3_vm_set_mem(vm, addr, &zero);
}

static void test_system_info_and_math(void)
{
    k3_vm vm;
    k3_vm_callbacks cb = { .on_syscall = k3_host_syscall };
    k3_vm_init(&vm, &cb);

    /* Test MEMSIZE */
    bool handled = k3_host_syscall(NULL, &vm, K3_SYS_MEMSIZE);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == K3_VM_MEMORY_WORDS);

    /* Test RANDOM */
    for (int i = 0; i < 20; ++i) {
        handled = k3_host_syscall(NULL, &vm, K3_SYS_RANDOM);
        CHECK(handled);
        int64_t r = helper_get_reg(&vm, 0);
        CHECK(r >= -K3_WORD_MAX && r <= K3_WORD_MAX);
    }

    /* Test RANDOM_TRIT */
    for (int i = 0; i < 20; ++i) {
        handled = k3_host_syscall(NULL, &vm, K3_SYS_RANDOM_TRIT);
        CHECK(handled);
        int64_t t = helper_get_reg(&vm, 0);
        CHECK(t == -1 || t == 0 || t == 1);
    }

    /* Test TIME */
    handled = k3_host_syscall(NULL, &vm, K3_SYS_TIME);
    CHECK(handled);
    int64_t t0_low = helper_get_reg(&vm, 0);
    int64_t t0_high = helper_get_reg(&vm, 1);
    CHECK(t0_low >= -K3_WORD_MAX && t0_low <= K3_WORD_MAX);
    CHECK(t0_high >= -K3_WORD_MAX && t0_high <= K3_WORD_MAX);

    /* Test INSN_COUNT */
    vm.instruction_count = 1234;
    handled = k3_host_syscall(NULL, &vm, K3_SYS_INSN_COUNT);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == 1234);

    /* Test HOST_INFO */
    helper_set_reg(&vm, 0, 400); /* buf addr */
    helper_set_reg(&vm, 1, 64);  /* max len */
    helper_set_reg(&vm, 2, 0);   /* OS & kernel */
    handled = k3_host_syscall(NULL, &vm, K3_SYS_HOST_INFO);
    CHECK(handled);
    int64_t info_len = helper_get_reg(&vm, 0);
    CHECK(info_len > 0);

    /* Test DATETIME */
    handled = k3_host_syscall(NULL, &vm, K3_SYS_DATETIME);
    CHECK(handled);
    int64_t yr = helper_get_reg(&vm, 0);
    int64_t mo = helper_get_reg(&vm, 1);
    int64_t dy = helper_get_reg(&vm, 2);
    CHECK(yr >= 2024 && yr <= 2040);
    CHECK(mo >= 1 && mo <= 12);
    CHECK(dy >= 1 && dy <= 31);

    /* Test HOST_METRICS */
    handled = k3_host_syscall(NULL, &vm, K3_SYS_HOST_METRICS);
    CHECK(handled);
    int64_t cores = helper_get_reg(&vm, 0);
    int64_t ram = helper_get_reg(&vm, 1);
    CHECK(cores >= 1);
    CHECK(ram >= 100);

    /* Test SYSINFO & HELP */
    helper_set_reg(&vm, 0, 42);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_SYSINFO);
    CHECK(handled);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_HELP);
    CHECK(handled);

    /* Test DRAW_VRAM, GUI_DESKTOP & RAW_MODE */
    helper_set_reg(&vm, 0, 500);
    helper_set_reg(&vm, 1, 4);
    helper_set_reg(&vm, 2, 2);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_DRAW_VRAM);
    CHECK(handled);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_GUI_DESKTOP);
    CHECK(handled);
    helper_set_reg(&vm, 0, 0);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_RAW_MODE);
    CHECK(handled);

    /* Test EXIT */
    handled = k3_host_syscall(NULL, &vm, K3_SYS_EXIT);
    CHECK(handled);
    CHECK(vm.status == K3_VM_HALTED);
}

static void test_file_io(void)
{
    k3_vm vm;
    k3_vm_callbacks cb = { .on_syscall = k3_host_syscall };
    k3_vm_init(&vm, &cb);
    k3_syscall_reset();

    const char *test_path = "/tmp/k3_test_syscall.tmp";
    helper_write_mem_str(&vm, 100, test_path);

    /* 1. Open for writing (mode 1) */
    helper_set_reg(&vm, 0, 100); /* path addr */
    helper_set_reg(&vm, 1, 1);   /* mode: write */
    bool handled = k3_host_syscall(NULL, &vm, K3_SYS_OPEN);
    CHECK(handled);
    int64_t fd = helper_get_reg(&vm, 0);
    CHECK(fd >= 3 && fd < K3_MAX_HOST_FDS);

    /* 2. Write data to file */
    const char *msg = "K-Three OS VFS Test 123";
    size_t msg_len = strlen(msg);
    helper_write_mem_str(&vm, 200, msg);
    helper_set_reg(&vm, 0, fd);
    helper_set_reg(&vm, 1, 200); /* buf addr */
    helper_set_reg(&vm, 2, (int64_t)msg_len);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_WRITE_FD);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == (int64_t)msg_len);

    /* 3. Close file */
    helper_set_reg(&vm, 0, fd);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_CLOSE);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == 0);

    /* 4. Open for reading (mode 0) */
    helper_set_reg(&vm, 0, 100);
    helper_set_reg(&vm, 1, 0);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_OPEN);
    CHECK(handled);
    fd = helper_get_reg(&vm, 0);
    CHECK(fd >= 3);

    /* 5. Read back into memory at 300 */
    helper_set_reg(&vm, 0, fd);
    helper_set_reg(&vm, 1, 300);
    helper_set_reg(&vm, 2, 64);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_READ);
    CHECK(handled);
    int64_t bytes_read = helper_get_reg(&vm, 0);
    CHECK(bytes_read == (int64_t)msg_len);

    /* Verify read bytes */
    for (size_t i = 0; i < msg_len; ++i) {
        bt_word w;
        int64_t ch = 0;
        k3_vm_get_mem(&vm, (uint16_t)(300 + i), &w);
        bt_word_to_i64(&w, &ch);
        CHECK(ch == (unsigned char)msg[i]);
    }

    /* 6. Seek test */
    helper_set_reg(&vm, 0, fd);
    helper_set_reg(&vm, 1, 0);
    helper_set_reg(&vm, 2, 0); /* SEEK_SET */
    handled = k3_host_syscall(NULL, &vm, K3_SYS_SEEK);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == 0);

    /* Close */
    helper_set_reg(&vm, 0, fd);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_CLOSE);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == 0);

    /* 7. Rename the file */
    const char *renamed_path = "/tmp/k3_test_syscall_renamed.tmp";
    helper_write_mem_str(&vm, 400, renamed_path);
    helper_set_reg(&vm, 0, 100);
    helper_set_reg(&vm, 1, 400);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_RENAME);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == 0);
    {
        FILE *f = fopen(test_path, "r");
        CHECK(f == NULL);
        f = fopen(renamed_path, "r");
        CHECK(f != NULL);
        if (f != NULL) {
            fclose(f);
        }
    }

    /* 8. Query file size and existence before removal */
    helper_set_reg(&vm, 0, 400);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_FILE_SIZE);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == (int64_t)msg_len);

    helper_set_reg(&vm, 0, 400);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_FILE_EXISTS);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == 1);

    /* 9. Remove the renamed file */
    helper_set_reg(&vm, 0, 400);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_REMOVE);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == 0);

    /* 10. Renaming a missing path fails */
    helper_set_reg(&vm, 0, 100);
    helper_set_reg(&vm, 1, 400);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_RENAME);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == -1);

    /* 11. Size of missing file returns -1, existence returns 0 */
    helper_set_reg(&vm, 0, 400);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_FILE_SIZE);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == -1);

    helper_set_reg(&vm, 0, 400);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_FILE_EXISTS);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == 0);

    /* Clean up */
    k3_syscall_reset();
}

static void test_bank_switching(void)
{
    k3_vm vm;
    k3_vm_callbacks cb = { .on_syscall = k3_host_syscall };
    k3_vm_init(&vm, &cb);
    k3_syscall_reset();

    /* Slot 7 is at memory address 7 * 729 = 5103 .. 5831 */
    uint16_t slot7_addr = (uint16_t)(7 * 729);

    /* Write a unique value in slot 7 (bank 7) */
    bt_word val1;
    bt_word_from_i64(1234, &val1);
    CHECK(k3_vm_set_mem(&vm, slot7_addr, &val1));

    /* Map bank 50 into slot 7 */
    helper_set_reg(&vm, 0, 7);  /* slot */
    helper_set_reg(&vm, 1, 50); /* new bank ID */
    bool handled = k3_host_syscall(NULL, &vm, K3_SYS_BANK_SWITCH);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == 7); /* old bank was 7 */

    /* Slot 7 should now be zeroed (bank 50 is freshly allocated) */
    bt_word read_val;
    int64_t num = -1;
    CHECK(k3_vm_get_mem(&vm, slot7_addr, &read_val));
    CHECK(bt_word_to_i64(&read_val, &num));
    CHECK(num == 0);

    /* Write a new value into bank 50 */
    bt_word val2;
    bt_word_from_i64(5678, &val2);
    CHECK(k3_vm_set_mem(&vm, slot7_addr, &val2));

    /* Map bank 7 back into slot 7 */
    helper_set_reg(&vm, 0, 7);
    helper_set_reg(&vm, 1, 7);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_BANK_SWITCH);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == 50); /* old bank was 50 */

    /* Previous bank 7 content (1234) must be restored */
    CHECK(k3_vm_get_mem(&vm, slot7_addr, &read_val));
    CHECK(bt_word_to_i64(&read_val, &num));
    CHECK(num == 1234);

    /* Out of bounds slot returns -1 */
    helper_set_reg(&vm, 0, 8);
    helper_set_reg(&vm, 1, 10);
    handled = k3_host_syscall(NULL, &vm, K3_SYS_BANK_SWITCH);
    CHECK(handled);
    CHECK(helper_get_reg(&vm, 0) == -1);

    k3_syscall_reset();
}

int main(void)
{
    test_system_info_and_math();
    test_file_io();
    test_bank_switching();

    printf("test_syscall: %d checks, %d failures\n", g_checks, g_failures);
    return (g_failures == 0) ? 0 : 1;
}
