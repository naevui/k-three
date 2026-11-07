#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/select.h>

#include "io.h"
#include "ternary.h"
#include "vm.h"

typedef struct {
    uint16_t base;
    uint16_t size;
    k3_mmio_read_fn read_fn;
    k3_mmio_write_fn write_fn;
    void *user;
} mmio_device_entry;

static mmio_device_entry s_devices[K3_MMIO_MAX_DEVICES];
static size_t s_device_count = 0;

static int64_t s_leds = 0;
static int64_t s_timer_interval = 0;
static int64_t s_timer_ctrl = 0;
static int64_t s_uart_ctrl = 0;
static struct timespec s_boot_time;
static bool s_time_initialized = false;
static uint64_t s_rng_state = 0x123456789ABCDEFULL;

/* Block Storage Device State */
static bt_word s_disk[K3_DISK_MAX_SECTORS * K3_DISK_SECTOR_WORDS];
static int64_t s_disk_sector = 0;
static int64_t s_disk_dma_addr = 0;
static int64_t s_disk_count = K3_DISK_SECTOR_WORDS;
static int64_t s_disk_status = K3_DISK_STATUS_IDLE;
static char s_disk_path[512] = { 0 };

/* Video RAM & Display Controller State */
static bt_word s_vram[K3_MMIO_VRAM_SIZE];

static uint64_t xorshift64(void)
{
    uint64_t x = s_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    s_rng_state = x;
    return x;
}

static void init_time_base(void)
{
    if (!s_time_initialized) {
        if (clock_gettime(CLOCK_MONOTONIC, &s_boot_time) != 0) {
            s_boot_time.tv_sec = 0;
            s_boot_time.tv_nsec = 0;
        }
        s_rng_state ^= (uint64_t)s_boot_time.tv_nsec;
        s_time_initialized = true;
    }
}

static int64_t get_monotonic_ms(void)
{
    init_time_base();
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    int64_t sec = (int64_t)(now.tv_sec - s_boot_time.tv_sec);
    int64_t nsec = (int64_t)(now.tv_nsec - s_boot_time.tv_nsec);
    return sec * 1000 + nsec / 1000000;
}

static bool stdin_has_data(void)
{
    fd_set fds;
    struct timeval tv = { 0, 0 };
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

/* Built-in standard peripheral handler */
static bool standard_peripheral_read(k3_vm *vm, uint16_t address, bt_word *out)
{
    int64_t val = 0;

    switch (address) {
    case K3_MMIO_UART_DATA: {
        int ch = EOF;
        if (stdin_has_data()) {
            unsigned char c = 0;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                ch = (int)c;
            }
        }
        val = (ch == EOF) ? -1 : (int64_t)ch;
        break;
    }

    case K3_MMIO_UART_STAT: {
        int64_t rx_ready = stdin_has_data() ? 1 : 0;
        int64_t tx_ready = 1;
        val = rx_ready | (tx_ready << 1);
        break;
    }

    case K3_MMIO_UART_CTRL:
        val = s_uart_ctrl;
        break;

    case K3_MMIO_TIMER_L: {
        int64_t ms = get_monotonic_ms();
        int64_t high = ms / 19683;
        int64_t low = ms - high * 19683;
        if (low > K3_WORD_MAX) {
            low -= 19683;
        } else if (low < -K3_WORD_MAX) {
            low += 19683;
        }
        val = low;
        break;
    }

    case K3_MMIO_TIMER_H: {
        int64_t ms = get_monotonic_ms();
        int64_t high = ms / 19683;
        if (high > K3_WORD_MAX) high = K3_WORD_MAX;
        if (high < -K3_WORD_MAX) high = -K3_WORD_MAX;
        val = high;
        break;
    }

    case K3_MMIO_TIMER_INT:
        val = s_timer_interval;
        break;

    case K3_MMIO_TIMER_CTRL:
        val = s_timer_ctrl | ((vm && (vm->irq_pending & 1)) ? 8 : 0);
        break;

    case K3_MMIO_SYS_IRQ_CTRL: {
        int64_t enabled = vm ? vm->irq_enabled : 0;
        int64_t pending = vm ? vm->irq_pending : 0;
        int64_t active = vm ? (int64_t)(vm->active_irq + 1) : 0;
        val = enabled | (pending << 1) | (active << 9);
        break;
    }

    case K3_MMIO_RNG_WORD: {
        uint64_t r = xorshift64();
        val = (int64_t)(r % (uint64_t)(2 * K3_WORD_MAX + 1)) - K3_WORD_MAX;
        break;
    }

    case K3_MMIO_RNG_TRIT: {
        uint64_t r = xorshift64();
        val = (int64_t)(r % 3) - 1;
        break;
    }

    case K3_MMIO_SYS_RESET:
    case K3_MMIO_SYS_HALT:
        val = 0;
        break;

    case K3_MMIO_SYS_LEDS:
        val = s_leds;
        break;

    case K3_MMIO_SYS_TICKS_L: {
        uint64_t count = vm ? vm->instruction_count : 0;
        int64_t high = (int64_t)(count / 19683);
        int64_t low = (int64_t)(count - (uint64_t)high * 19683);
        if (low > K3_WORD_MAX) low -= 19683;
        val = low;
        break;
    }

    case K3_MMIO_SYS_TICKS_H: {
        uint64_t count = vm ? vm->instruction_count : 0;
        int64_t high = (int64_t)(count / 19683);
        if (high > K3_WORD_MAX) high = K3_WORD_MAX;
        val = high;
        break;
    }

    case K3_MMIO_SYS_MEMSIZE:
        val = K3_VM_MEMORY_WORDS;
        break;

    case K3_MMIO_SYS_USER_BASE:
        val = vm ? vm->user_base : 0;
        break;

    case K3_MMIO_SYS_USER_LIMIT:
        val = vm ? vm->user_limit : K3_VM_MAX_ADDRESS;
        break;

    /* Block Device / Disk Registers */
    case K3_MMIO_DISK_SECTOR:
        val = s_disk_sector;
        break;

    case K3_MMIO_DISK_DMA_ADDR:
        val = s_disk_dma_addr;
        break;

    case K3_MMIO_DISK_COUNT:
        val = s_disk_count;
        break;

    case K3_MMIO_DISK_CMD:
        val = 0;
        break;

    case K3_MMIO_DISK_STATUS:
        val = s_disk_status;
        break;

    case K3_MMIO_DISK_CAPACITY:
        val = K3_DISK_MAX_SECTORS;
        break;

    case K3_MMIO_VRAM_CTRL:
        val = 0;
        break;

    case K3_MMIO_VRAM_COLS:
        val = 20;
        break;

    case K3_MMIO_VRAM_ROWS:
        val = 5;
        break;

    default:
        return false;
    }

    return bt_word_from_i64(val, out);
}

static bool standard_peripheral_write(k3_vm *vm, uint16_t address, const bt_word *value)
{
    int64_t val = 0;
    if (!bt_word_to_i64(value, &val)) {
        return false;
    }

    switch (address) {
    case K3_MMIO_UART_DATA: {
        int ch = (int)(val & 0xFF);
        putchar(ch);
        if (ch == '\n') {
            fflush(stdout);
        }
        return true;
    }

    case K3_MMIO_UART_CTRL:
        s_uart_ctrl = val;
        return true;

    case K3_MMIO_TIMER_INT:
        s_timer_interval = val;
        return true;

    case K3_MMIO_TIMER_CTRL:
        s_timer_ctrl = val;
        if (vm != NULL) {
            if (val & 2) {
                k3_vm_clear_irq(vm, 0);
            }
            if (val & 4) {
                k3_vm_raise_irq(vm, 0);
            }
        }
        return true;

    case K3_MMIO_SYS_IRQ_CTRL:
        if (vm != NULL) {
            if (val & 1) {
                vm->irq_enabled = 1;
            } else if (val & (1 << 8)) {
                vm->irq_enabled = 0;
            }
            uint8_t to_clear = (uint8_t)((val >> 1) & 0x7F);
            vm->irq_pending &= ~to_clear;
            uint8_t to_raise = (uint8_t)((val >> 9) & 0xFF);
            vm->irq_pending |= to_raise;
        }
        return true;

    case K3_MMIO_SYS_RESET:
        if (val != 0 && vm != NULL) {
            k3_vm_reset(vm);
        }
        return true;

    case K3_MMIO_SYS_HALT:
        if (val != 0 && vm != NULL) {
            vm->status = K3_VM_HALTED;
        }
        return true;

    case K3_MMIO_SYS_LEDS:
        s_leds = val;
        return true;

    case K3_MMIO_SYS_USER_BASE:
        if (vm != NULL && vm->mode == K3_MODE_KERNEL) {
            vm->user_base = (val >= 0 && val <= K3_VM_MAX_ADDRESS) ? (uint16_t)val : 0;
        }
        return true;

    case K3_MMIO_SYS_USER_LIMIT:
        if (vm != NULL && vm->mode == K3_MODE_KERNEL) {
            vm->user_limit = (val >= 0 && val <= K3_VM_MAX_ADDRESS) ? (uint16_t)val : K3_VM_MAX_ADDRESS;
        }
        return true;

    /* Block Device / Disk Registers */
    case K3_MMIO_DISK_SECTOR:
        s_disk_sector = val;
        return true;

    case K3_MMIO_DISK_DMA_ADDR:
        s_disk_dma_addr = val;
        return true;

    case K3_MMIO_DISK_COUNT:
        s_disk_count = val;
        return true;

    case K3_MMIO_DISK_CMD:
        if (vm == NULL) {
            s_disk_status = K3_DISK_STATUS_ERROR;
            return true;
        }
        if (val == K3_DISK_CMD_READ) {
            if (s_disk_sector < 0 || s_disk_sector >= K3_DISK_MAX_SECTORS ||
                s_disk_count <= 0 || s_disk_count > K3_DISK_SECTOR_WORDS ||
                s_disk_dma_addr < 0 || s_disk_dma_addr + s_disk_count > K3_VM_MEMORY_WORDS) {
                s_disk_status = K3_DISK_STATUS_ERROR;
            } else {
                size_t disk_offset = (size_t)s_disk_sector * K3_DISK_SECTOR_WORDS;
                memcpy(&vm->memory[s_disk_dma_addr], &s_disk[disk_offset],
                       (size_t)s_disk_count * sizeof(bt_word));
                s_disk_status = K3_DISK_STATUS_IDLE;
            }
        } else if (val == K3_DISK_CMD_WRITE) {
            if (s_disk_sector < 0 || s_disk_sector >= K3_DISK_MAX_SECTORS ||
                s_disk_count <= 0 || s_disk_count > K3_DISK_SECTOR_WORDS ||
                s_disk_dma_addr < 0 || s_disk_dma_addr + s_disk_count > K3_VM_MEMORY_WORDS) {
                s_disk_status = K3_DISK_STATUS_ERROR;
            } else {
                size_t disk_offset = (size_t)s_disk_sector * K3_DISK_SECTOR_WORDS;
                memcpy(&s_disk[disk_offset], &vm->memory[s_disk_dma_addr],
                       (size_t)s_disk_count * sizeof(bt_word));
                s_disk_status = K3_DISK_STATUS_IDLE;
            }
        } else if (val == K3_DISK_CMD_SYNC) {
            s_disk_status = K3_DISK_STATUS_IDLE;
        } else {
            s_disk_status = K3_DISK_STATUS_ERROR;
        }
        return true;

    case K3_MMIO_VRAM_CTRL:
        if (val == 1) {
            printf("+--------------------+\n");
            for (int r = 0; r < 5; ++r) {
                putchar('|');
                for (int c = 0; c < 20; ++c) {
                    int64_t ch_val = 0;
                    bt_word_to_i64(&s_vram[r * 20 + c], &ch_val);
                    int ch = (int)(ch_val & 0xFF);
                    if (ch < 32 || ch > 126) ch = ' ';
                    putchar(ch);
                }
                printf("|\n");
            }
            printf("+--------------------+\n");
            fflush(stdout);
        }
        return true;

    case K3_MMIO_GUI_LAUNCH:
        /* Native Wayland GUI request acknowledged */
        return true;

    default:
        return false;
    }
}

void k3_io_init(void)
{
    s_device_count = 0;
    s_leds = 0;
    s_timer_interval = 0;
    s_timer_ctrl = 0;
    s_uart_ctrl = 0;
    s_disk_sector = 0;
    s_disk_dma_addr = 0;
    s_disk_count = K3_DISK_SECTOR_WORDS;
    s_disk_status = K3_DISK_STATUS_IDLE;
    for (size_t i = 0; i < K3_DISK_MAX_SECTORS * K3_DISK_SECTOR_WORDS; ++i) {
        bt_word_zero(&s_disk[i]);
    }
    for (size_t i = 0; i < K3_MMIO_VRAM_SIZE; ++i) {
        bt_word_zero(&s_vram[i]);
    }
    init_time_base();
}

bool k3_io_disk_attach(const char *image_path)
{
    if (image_path == NULL) {
        s_disk_path[0] = '\0';
        return true;
    }
    strncpy(s_disk_path, image_path, sizeof(s_disk_path) - 1);
    s_disk_path[sizeof(s_disk_path) - 1] = '\0';
    return true;
}

void k3_io_reset(void)
{
    k3_io_init();
}

bool k3_io_register_device(uint16_t base, uint16_t size,
                           k3_mmio_read_fn read_fn,
                           k3_mmio_write_fn write_fn,
                           void *user)
{
    if (s_device_count >= K3_MMIO_MAX_DEVICES || size == 0) {
        return false;
    }
    s_devices[s_device_count].base = base;
    s_devices[s_device_count].size = size;
    s_devices[s_device_count].read_fn = read_fn;
    s_devices[s_device_count].write_fn = write_fn;
    s_devices[s_device_count].user = user;
    s_device_count++;
    return true;
}

bool k3_io_mmio_read(void *user, k3_vm *vm, uint16_t address, bt_word *out)
{
    (void)user;

    /* Check standard peripheral range */
    if (address >= K3_MMIO_BASE && address <= (K3_MMIO_BASE + 31)) {
        if (standard_peripheral_read(vm, address, out)) {
            return true;
        }
    }

    /* Check Video RAM range 5030..5129 */
    if (address >= K3_MMIO_VRAM_BASE && address <= K3_MMIO_VRAM_END) {
        *out = s_vram[address - K3_MMIO_VRAM_BASE];
        return true;
    }

    /* Check registered custom devices */
    for (size_t i = 0; i < s_device_count; ++i) {
        if (address >= s_devices[i].base &&
            address < s_devices[i].base + s_devices[i].size) {
            if (s_devices[i].read_fn != NULL) {
                return s_devices[i].read_fn(s_devices[i].user, vm, address, out);
            }
        }
    }

    return false;
}

bool k3_io_mmio_write(void *user, k3_vm *vm, uint16_t address, const bt_word *value)
{
    (void)user;

    /* Check standard peripheral range */
    if (address >= K3_MMIO_BASE && address <= (K3_MMIO_BASE + 31)) {
        if (standard_peripheral_write(vm, address, value)) {
            return true;
        }
    }

    /* Check Video RAM range 5030..5129 */
    if (address >= K3_MMIO_VRAM_BASE && address <= K3_MMIO_VRAM_END) {
        s_vram[address - K3_MMIO_VRAM_BASE] = *value;
        return true;
    }

    /* Check registered custom devices */
    for (size_t i = 0; i < s_device_count; ++i) {
        if (address >= s_devices[i].base &&
            address < s_devices[i].base + s_devices[i].size) {
            if (s_devices[i].write_fn != NULL) {
                return s_devices[i].write_fn(s_devices[i].user, vm, address, value);
            }
        }
    }

    return false;
}

void k3_io_attach(k3_vm *vm)
{
    if (vm == NULL) return;
    vm->callbacks.on_mmio_read = k3_io_mmio_read;
    vm->callbacks.on_mmio_write = k3_io_mmio_write;
}

int64_t k3_io_get_leds(void)
{
    return s_leds;
}

void k3_io_set_leds(int64_t val)
{
    s_leds = val;
}
