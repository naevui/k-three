/*
 * K-Three Memory-Mapped I/O (MMIO) Hardware Peripheral Architecture.
 *
 * Strict C11, balanced-ternary architecture.
 */

#ifndef K3_IO_H
#define K3_IO_H

#include <stdbool.h>
#include <stdint.h>

#include "vm.h"

/*
 * Standard K-Three MMIO Peripheral Address Map (Page 6).
 *
 * Registers are addressed as normal memory words via LOAD and STORE:
 *   5000: IO_UART_DATA   - R: read char (-1 on EOF), W: write char to console
 *   5001: IO_UART_STAT   - R: status trits (trit 0 = RX ready, trit 1 = TX ready)
 *   5002: IO_UART_CTRL   - R/W: serial control register
 *   5004: IO_TIMER_L     - R: monotonic ms timestamp (low 9 trits)
 *   5005: IO_TIMER_H     - R: monotonic ms timestamp (high 9 trits)
 *   5006: IO_TIMER_INT   - R/W: periodic timer interval trigger
 *   5007: IO_TIMER_CTRL  - R/W: timer control register
 *   5008: IO_RNG_WORD    - R: hardware pseudo-random 9-trit word [-9841..9841]
 *   5009: IO_RNG_TRIT    - R: hardware random balanced trit (-1, 0, +1)
 *   5010: IO_SYS_RESET   - W: write 1 to trigger CPU reset
 *   5011: IO_SYS_HALT    - W: write 1 to halt CPU
 *   5012: IO_SYS_LEDS    - R/W: 9-trit front-panel status LED word
 *   5013: IO_SYS_TICKS_L - R: CPU instruction count (low 9 trits)
 *   5014: IO_SYS_TICKS_H - R: CPU instruction count (high 9 trits)
 *   5015: IO_SYS_MEMSIZE - R: total memory words (6561)
 */

#define K3_MMIO_BASE            5000
#define K3_MMIO_UART_DATA       5000
#define K3_MMIO_UART_STAT       5001
#define K3_MMIO_UART_CTRL       5002
#define K3_MMIO_TIMER_L         5004
#define K3_MMIO_TIMER_H         5005
#define K3_MMIO_TIMER_INT       5006
#define K3_MMIO_TIMER_CTRL      5007
#define K3_MMIO_RNG_WORD        5008
#define K3_MMIO_RNG_TRIT        5009
#define K3_MMIO_SYS_RESET       5010
#define K3_MMIO_SYS_HALT        5011
#define K3_MMIO_SYS_LEDS        5012
#define K3_MMIO_SYS_TICKS_L     5013
#define K3_MMIO_SYS_TICKS_H     5014
#define K3_MMIO_SYS_MEMSIZE     5015

#define K3_MMIO_MAX_DEVICES     16

typedef bool (*k3_mmio_read_fn)(void *user, k3_vm *vm, uint16_t address, bt_word *out);
typedef bool (*k3_mmio_write_fn)(void *user, k3_vm *vm, uint16_t address, const bt_word *value);

/* Initialize or reset the I/O subsystem state */
void k3_io_init(void);
void k3_io_reset(void);

/* Default VM MMIO callbacks to install in k3_vm_callbacks */
bool k3_io_mmio_read(void *user, k3_vm *vm, uint16_t address, bt_word *out);
bool k3_io_mmio_write(void *user, k3_vm *vm, uint16_t address, const bt_word *value);

/* Attach standard MMIO handlers to a VM callback structure */
void k3_io_attach(k3_vm *vm);

/* Register custom MMIO peripheral device */
bool k3_io_register_device(uint16_t base, uint16_t size,
                           k3_mmio_read_fn read_fn,
                           k3_mmio_write_fn write_fn,
                           void *user);

/* Access front-panel LED state (for diagnostics/GUI/TUI) */
int64_t k3_io_get_leds(void);
void k3_io_set_leds(int64_t val);

#endif /* K3_IO_H */
