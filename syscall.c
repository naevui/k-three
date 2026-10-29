#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE

#include "syscall.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>

#include "image.h"
#include "ternary.h"

static FILE *g_host_fds[K3_MAX_HOST_FDS];
static bool g_fds_initialized = false;
static uint64_t g_boot_millis = 0;
static bool g_boot_initialized = false;

static uint64_t host_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void init_time(void)
{
    if (!g_boot_initialized) {
        g_boot_millis = host_now_ms();
        g_boot_initialized = true;
    }
}

static void init_fds(void)
{
    if (g_fds_initialized) return;
    g_host_fds[0] = stdin;
    g_host_fds[1] = stdout;
    g_host_fds[2] = stderr;
    for (int i = 3; i < K3_MAX_HOST_FDS; ++i) {
        g_host_fds[i] = NULL;
    }
    g_fds_initialized = true;
}

static struct termios g_orig_termios;
static bool g_in_raw_mode = false;

static void set_raw_mode(bool enable)
{
    if (enable && !g_in_raw_mode && isatty(STDIN_FILENO)) {
        tcgetattr(STDIN_FILENO, &g_orig_termios);
        struct termios raw = g_orig_termios;
        raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        g_in_raw_mode = true;
    } else if (!enable && g_in_raw_mode && isatty(STDIN_FILENO)) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
        g_in_raw_mode = false;
    }
}

void k3_syscall_reset(void)
{
    set_raw_mode(false);
    init_fds();
    g_boot_millis = host_now_ms();
    g_boot_initialized = true;
    for (int i = 3; i < K3_MAX_HOST_FDS; ++i) {
        if (g_host_fds[i] != NULL) {
            fclose(g_host_fds[i]);
            g_host_fds[i] = NULL;
        }
    }
}

static int64_t reg_val(const k3_vm *vm, unsigned r)
{
    int64_t val = 0;
    bt_word w;
    if (k3_vm_get_reg(vm, (uint8_t)r, &w)) {
        (void)bt_word_to_i64(&w, &val);
    }
    return val;
}

static void set_reg_val(k3_vm *vm, unsigned r, int64_t val)
{
    bt_word w;
    bt_word_zero(&w);
    if (val < -K3_WORD_MAX) val = -K3_WORD_MAX;
    if (val > K3_WORD_MAX) val = K3_WORD_MAX;
    (void)bt_word_from_i64(val, &w);
    (void)k3_vm_set_reg(vm, (uint8_t)r, &w);
}

static bool read_mem_string(const k3_vm *vm, int64_t addr, char *dst, size_t max_len)
{
    if (addr < 0 || addr > K3_VM_MAX_ADDRESS || max_len == 0) {
        return false;
    }
    size_t i = 0;
    while (i + 1 < max_len && (addr + (int64_t)i) <= K3_VM_MAX_ADDRESS) {
        bt_word w;
        int64_t ch = 0;
        if (!k3_vm_get_mem(vm, (uint16_t)(addr + i), &w)) {
            break;
        }
        (void)bt_word_to_i64(&w, &ch);
        if (ch == 0) {
            break;
        }
        dst[i++] = (char)(ch & 0xFF);
    }
    dst[i] = '\0';
    return true;
}

static int64_t write_mem_string(k3_vm *vm, int64_t addr, const char *src, int64_t max_len)
{
    if (addr < 0 || addr > K3_VM_MAX_ADDRESS || max_len <= 0 || src == NULL) {
        return 0;
    }
    int64_t count = 0;
    while (*src && count + 1 < max_len && (addr + count) <= K3_VM_MAX_ADDRESS) {
        bt_word w;
        bt_word_from_i64((unsigned char)*src, &w);
        k3_vm_set_mem(vm, (uint16_t)(addr + count), &w);
        src++;
        count++;
    }
    if ((addr + count) <= K3_VM_MAX_ADDRESS) {
        bt_word zero;
        bt_word_zero(&zero);
        k3_vm_set_mem(vm, (uint16_t)(addr + count), &zero);
    }
    return count;
}

static int host_keyhit(void)
{
    struct timeval tv = { 0, 0 };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    int res = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
    return (res > 0) ? 1 : 0;
}

static void run_desktop_app(const char *path, char *status_buf, size_t status_cap)
{
    bt_word child_words[K3_VM_MEMORY_WORDS];
    size_t count = 0;
    if (!k3_image_read(path, child_words, K3_VM_MEMORY_WORDS, &count)) {
        snprintf(status_buf, status_cap, "\033[1;31mERR: Cannot open %s\033[0m", path);
        return;
    }
    k3_vm child_vm;
    k3_vm_callbacks cb = {
        .user = NULL,
        .on_step = NULL,
        .on_stop = NULL,
        .on_syscall = k3_host_syscall,
    };
    k3_vm_init(&child_vm, &cb);
    if (!k3_vm_load(&child_vm, child_words, count, 0)) {
        snprintf(status_buf, status_cap, "\033[1;31mERR: Failed to load %s\033[0m", path);
        return;
    }
    uint64_t executed = 0;
    k3_vm_status st = k3_vm_run(&child_vm, 1000000, &executed);
    int64_t r0 = 0, r1 = 0;
    bt_word w0, w1;
    if (k3_vm_get_reg(&child_vm, 0, &w0)) (void)bt_word_to_i64(&w0, &r0);
    if (k3_vm_get_reg(&child_vm, 1, &w1)) (void)bt_word_to_i64(&w1, &r1);

    const char *st_name = (st == K3_VM_HALTED) ? "HALTED" : ((st == K3_VM_TRAPPED) ? "TRAPPED" : "RUNNING");
    snprintf(status_buf, status_cap, "\033[1;32m%s\033[0m => \033[1;33m%s\033[0m (r0=%lld, r1=%lld, %llu steps)",
             path, st_name, (long long)r0, (long long)r1, (unsigned long long)executed);
}

static void render_bar(char *out, size_t cap, int pct, int width)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    int filled = (pct * width) / 100;
    size_t pos = 0;
    for (int i = 0; i < filled && pos + 4 < cap; ++i) {
        pos += snprintf(out + pos, cap - pos, "█");
    }
    for (int i = filled; i < width && pos + 4 < cap; ++i) {
        pos += snprintf(out + pos, cap - pos, "░");
    }
    out[pos] = '\0';
}

static const int8_t g_sin32[32] = {
    0, 24, 48, 70, 89, 104, 115, 122,
    124, 122, 115, 104, 89, 70, 48, 24,
    0, -24, -48, -70, -89, -104, -115, -122,
    -124, -122, -115, -104, -89, -70, -48, -24
};

bool k3_host_syscall(void *user, k3_vm *vm, int32_t code)
{
    (void)user;
    init_fds();
    init_time();

    switch (code) {
    case K3_SYS_EXIT: {
        set_raw_mode(false);
        vm->status = K3_VM_HALTED;
        vm->trap_reason = K3_TRAP_NONE;
        vm->trap_code = 0;
        vm->stop_pc = vm->last_pc;
        return true;
    }

    case K3_SYS_PUTC: {
        int64_t ch = reg_val(vm, 0);
        putchar((int)(ch & 0xFF));
        if (ch == '\n') {
            fflush(stdout);
        }
        return true;
    }

    case K3_SYS_GETC: {
        int ch = EOF;
        if (g_in_raw_mode) {
            unsigned char c = 0;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                ch = c;
            }
        } else {
            ch = getchar();
        }
        set_reg_val(vm, 0, (ch == EOF) ? -1 : (int64_t)ch);
        return true;
    }

    case K3_SYS_PUTNUM: {
        int64_t num = reg_val(vm, 0);
        printf("%lld", (long long)num);
        fflush(stdout);
        return true;
    }

    case K3_SYS_PUTS: {
        int64_t addr = reg_val(vm, 0);
        while (addr >= 0 && addr <= K3_VM_MAX_ADDRESS) {
            bt_word w;
            int64_t ch = 0;
            if (!k3_vm_get_mem(vm, (uint16_t)addr, &w)) {
                break;
            }
            (void)bt_word_to_i64(&w, &ch);
            if (ch == 0) {
                break;
            }
            putchar((int)(ch & 0xFF));
            addr++;
        }
        fflush(stdout);
        return true;
    }

    case K3_SYS_GETNUM: {
        long long num = 0;
        if (scanf("%lld", &num) == 1) {
            set_reg_val(vm, 0, (int64_t)num);
        } else {
            set_reg_val(vm, 0, 0);
        }
        return true;
    }

    case K3_SYS_PUTTRITS: {
        bt_word w;
        if (k3_vm_get_reg(vm, 0, &w)) {
            char trits[K3_TRITS + 1];
            for (size_t i = 0; i < K3_TRITS; ++i) {
                trits[i] = w.t[i] < 0 ? '-' : (w.t[i] > 0 ? '+' : '0');
            }
            trits[K3_TRITS] = '\0';
            printf("%s", trits);
            fflush(stdout);
        }
        return true;
    }

    case K3_SYS_GETS: {
        int64_t addr = reg_val(vm, 0);
        int64_t max_len = reg_val(vm, 1);
        if (addr < 0 || addr > K3_VM_MAX_ADDRESS || max_len <= 1) {
            set_reg_val(vm, 0, -1);
            return true;
        }
        int64_t count = 0;
        int ch;
        while (count + 1 < max_len && (addr + count) <= K3_VM_MAX_ADDRESS) {
            ch = getchar();
            if (ch == EOF) {
                if (count == 0) count = -1;
                break;
            }
            bt_word w;
            bt_word_from_i64(ch, &w);
            k3_vm_set_mem(vm, (uint16_t)(addr + count), &w);
            count++;
            if (ch == '\n') break;
        }
        if (count >= 0 && (addr + count) <= K3_VM_MAX_ADDRESS) {
            bt_word zero_w;
            bt_word_zero(&zero_w);
            k3_vm_set_mem(vm, (uint16_t)(addr + count), &zero_w);
        }
        set_reg_val(vm, 0, count);
        return true;
    }

    case K3_SYS_WRITE: {
        int64_t addr = reg_val(vm, 0);
        int64_t len = reg_val(vm, 1);
        int64_t written = 0;
        for (int64_t i = 0; i < len && (addr + i) <= K3_VM_MAX_ADDRESS; ++i) {
            bt_word w;
            int64_t ch = 0;
            if (k3_vm_get_mem(vm, (uint16_t)(addr + i), &w)) {
                bt_word_to_i64(&w, &ch);
                putchar((int)(ch & 0xFF));
                written++;
            }
        }
        fflush(stdout);
        set_reg_val(vm, 0, written);
        return true;
    }

    case K3_SYS_RANDOM: {
        int64_t r = ((int64_t)rand() % (2 * K3_WORD_MAX + 1)) - K3_WORD_MAX;
        set_reg_val(vm, 0, r);
        return true;
    }

    case K3_SYS_RANDOM_TRIT: {
        int64_t r = (rand() % 3) - 1; /* -1, 0, +1 */
        set_reg_val(vm, 0, r);
        return true;
    }

    case K3_SYS_OPEN: {
        int64_t path_addr = reg_val(vm, 0);
        int64_t mode = reg_val(vm, 1);
        char path[256];
        if (!read_mem_string(vm, path_addr, path, sizeof(path))) {
            set_reg_val(vm, 0, -1);
            return true;
        }

        const char *fmode = "r";
        switch (mode) {
        case 0: fmode = "rb"; break;
        case 1: fmode = "wb"; break;
        case 2: fmode = "ab"; break;
        case 3: fmode = "r+b"; break;
        default: fmode = "rb"; break;
        }

        int free_fd = -1;
        for (int i = 3; i < K3_MAX_HOST_FDS; ++i) {
            if (g_host_fds[i] == NULL) {
                free_fd = i;
                break;
            }
        }

        if (free_fd < 0) {
            set_reg_val(vm, 0, -1);
            return true;
        }

        FILE *f = fopen(path, fmode);
        if (f == NULL) {
            set_reg_val(vm, 0, -1);
            return true;
        }

        g_host_fds[free_fd] = f;
        set_reg_val(vm, 0, free_fd);
        return true;
    }

    case K3_SYS_CLOSE: {
        int64_t fd = reg_val(vm, 0);
        if (fd < 3 || fd >= K3_MAX_HOST_FDS || g_host_fds[fd] == NULL) {
            set_reg_val(vm, 0, -1);
            return true;
        }
        int res = fclose(g_host_fds[fd]);
        g_host_fds[fd] = NULL;
        set_reg_val(vm, 0, (res == 0) ? 0 : -1);
        return true;
    }

    case K3_SYS_READ: {
        int64_t fd = reg_val(vm, 0);
        int64_t buf_addr = reg_val(vm, 1);
        int64_t max_count = reg_val(vm, 2);

        if (fd < 0 || fd >= K3_MAX_HOST_FDS || g_host_fds[fd] == NULL ||
            buf_addr < 0 || buf_addr > K3_VM_MAX_ADDRESS || max_count <= 0) {
            set_reg_val(vm, 0, -1);
            return true;
        }

        int64_t count = 0;
        FILE *f = g_host_fds[fd];
        for (int64_t i = 0; i < max_count && (buf_addr + i) <= K3_VM_MAX_ADDRESS; ++i) {
            int ch = fgetc(f);
            if (ch == EOF) {
                break;
            }
            bt_word w;
            bt_word_from_i64(ch, &w);
            k3_vm_set_mem(vm, (uint16_t)(buf_addr + i), &w);
            count++;
        }
        set_reg_val(vm, 0, count);
        return true;
    }

    case K3_SYS_WRITE_FD: {
        int64_t fd = reg_val(vm, 0);
        int64_t buf_addr = reg_val(vm, 1);
        int64_t count = reg_val(vm, 2);

        if (fd < 0 || fd >= K3_MAX_HOST_FDS || g_host_fds[fd] == NULL ||
            buf_addr < 0 || buf_addr > K3_VM_MAX_ADDRESS || count <= 0) {
            set_reg_val(vm, 0, -1);
            return true;
        }

        int64_t written = 0;
        FILE *f = g_host_fds[fd];
        for (int64_t i = 0; i < count && (buf_addr + i) <= K3_VM_MAX_ADDRESS; ++i) {
            bt_word w;
            int64_t ch = 0;
            if (k3_vm_get_mem(vm, (uint16_t)(buf_addr + i), &w)) {
                bt_word_to_i64(&w, &ch);
                if (fputc((int)(ch & 0xFF), f) != EOF) {
                    written++;
                }
            }
        }
        fflush(f);
        set_reg_val(vm, 0, written);
        return true;
    }

    case K3_SYS_SEEK: {
        int64_t fd = reg_val(vm, 0);
        int64_t offset = reg_val(vm, 1);
        int64_t whence = reg_val(vm, 2);

        if (fd < 3 || fd >= K3_MAX_HOST_FDS || g_host_fds[fd] == NULL) {
            set_reg_val(vm, 0, -1);
            return true;
        }

        int w = SEEK_SET;
        if (whence == 1) w = SEEK_CUR;
        else if (whence == 2) w = SEEK_END;

        if (fseek(g_host_fds[fd], (long)offset, w) != 0) {
            set_reg_val(vm, 0, -1);
            return true;
        }
        long pos = ftell(g_host_fds[fd]);
        set_reg_val(vm, 0, (int64_t)pos);
        return true;
    }

    case K3_SYS_REMOVE: {
        int64_t path_addr = reg_val(vm, 0);
        char path[256];
        if (!read_mem_string(vm, path_addr, path, sizeof(path))) {
            set_reg_val(vm, 0, -1);
            return true;
        }
        int res = remove(path);
        set_reg_val(vm, 0, (res == 0) ? 0 : -1);
        return true;
    }

    case K3_SYS_TIME: {
        init_time();
        uint64_t elapsed = host_now_ms() - g_boot_millis;
        int64_t low = (int64_t)(elapsed % (K3_WORD_MAX + 1));
        int64_t high = (int64_t)((elapsed / (K3_WORD_MAX + 1)) % (K3_WORD_MAX + 1));
        set_reg_val(vm, 0, low);
        set_reg_val(vm, 1, high);
        return true;
    }

    case K3_SYS_SLEEP: {
        int64_t ms = reg_val(vm, 0);
        if (ms > 0) {
            struct timespec req;
            req.tv_sec = (time_t)(ms / 1000);
            req.tv_nsec = (long)((ms % 1000) * 1000000L);
            nanosleep(&req, NULL);
        }
        return true;
    }

    case K3_SYS_CLEAR: {
        printf("\033[2J\033[H");
        fflush(stdout);
        return true;
    }

    case K3_SYS_SETCURSOR: {
        int64_t row = reg_val(vm, 0);
        int64_t col = reg_val(vm, 1);
        if (row < 1) row = 1;
        if (col < 1) col = 1;
        printf("\033[%lld;%lldH", (long long)row, (long long)col);
        fflush(stdout);
        return true;
    }

    case K3_SYS_KEYHIT: {
        set_reg_val(vm, 0, host_keyhit());
        return true;
    }

    case K3_SYS_MEMSIZE: {
        set_reg_val(vm, 0, (int64_t)K3_VM_MEMORY_WORDS);
        return true;
    }

    case K3_SYS_INSN_COUNT: {
        uint64_t insns = vm->instruction_count;
        int64_t mod = 19683;
        int64_t low = (int64_t)(insns % (uint64_t)mod);
        if (low > K3_WORD_MAX) low -= mod;
        int64_t high = (int64_t)((insns / (uint64_t)mod) % (uint64_t)mod);
        if (high > K3_WORD_MAX) high -= mod;
        set_reg_val(vm, 0, low);
        set_reg_val(vm, 1, high);
        return true;
    }

    case K3_SYS_PUTC_ERR: {
        int64_t ch = reg_val(vm, 0);
        fputc((int)(ch & 0xFF), stderr);
        if (ch == '\n') fflush(stderr);
        return true;
    }

    case K3_SYS_PUTS_ERR: {
        int64_t addr = reg_val(vm, 0);
        while (addr >= 0 && addr <= K3_VM_MAX_ADDRESS) {
            bt_word w;
            int64_t ch = 0;
            if (!k3_vm_get_mem(vm, (uint16_t)addr, &w)) {
                break;
            }
            (void)bt_word_to_i64(&w, &ch);
            if (ch == 0) {
                break;
            }
            fputc((int)(ch & 0xFF), stderr);
            addr++;
        }
        fflush(stderr);
        return true;
    }

    case K3_SYS_EXEC: {
        int64_t path_addr = reg_val(vm, 0);
        char path[256];
        if (!read_mem_string(vm, path_addr, path, sizeof(path))) {
            set_reg_val(vm, 0, -1);
            return true;
        }

        bt_word child_words[K3_VM_MEMORY_WORDS];
        size_t count = 0;
        if (!k3_image_read(path, child_words, K3_VM_MEMORY_WORDS, &count)) {
            set_reg_val(vm, 0, -1);
            return true;
        }

        k3_vm child_vm;
        k3_vm_callbacks cb = {
            .user = NULL,
            .on_step = NULL,
            .on_stop = NULL,
            .on_syscall = k3_host_syscall,
        };
        k3_vm_init(&child_vm, &cb);
        if (!k3_vm_load(&child_vm, child_words, count, 0)) {
            set_reg_val(vm, 0, -1);
            return true;
        }

        k3_vm_status st = k3_vm_run(&child_vm, 10000000, NULL);
        int64_t ret = (st == K3_VM_HALTED) ? 0 : (int64_t)st;
        set_reg_val(vm, 0, ret);
        return true;
    }

    case K3_SYS_PUTTRITS_COLOR: {
        bt_word w;
        if (k3_vm_get_reg(vm, 0, &w)) {
            for (size_t i = 0; i < K3_TRITS; ++i) {
                if (w.t[i] > 0) {
                    printf("\033[1;32m+\033[0m");
                } else if (w.t[i] < 0) {
                    printf("\033[1;31m-\033[0m");
                } else {
                    printf("\033[90m0\033[0m");
                }
            }
            fflush(stdout);
        }
        return true;
    }

    case K3_SYS_HOST_INFO: {
        int64_t buf_addr = reg_val(vm, 0);
        int64_t max_len = reg_val(vm, 1);
        int64_t sel = reg_val(vm, 2);

        struct utsname u;
        (void)uname(&u);

        struct sysinfo si;
        (void)sysinfo(&si);
        int cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
        long total_mb = (long)((si.totalram * si.mem_unit) / (1024 * 1024));

        char str[256];
        if (sel == 0) {
            snprintf(str, sizeof(str), "%s %s %s", u.sysname, u.release, u.machine);
        } else if (sel == 1) {
            snprintf(str, sizeof(str), "%s (%d cores, %ldMB)", u.nodename, cores, total_mb);
        } else if (sel == 2) {
            time_t now = time(NULL);
            struct tm tm;
            localtime_r(&now, &tm);
            char dt[32];
            strftime(dt, sizeof(dt), "%Y-%m-%d %H:%M", &tm);
            snprintf(str, sizeof(str), "Date: %s", dt);
        } else {
            snprintf(str, sizeof(str), "%s %s (%dc,%ldM)", u.sysname, u.machine, cores, total_mb);
        }

        int64_t written = write_mem_string(vm, buf_addr, str, max_len);
        set_reg_val(vm, 0, written);
        return true;
    }

    case K3_SYS_DATETIME: {
        time_t now = time(NULL);
        struct tm tm;
        localtime_r(&now, &tm);
        set_reg_val(vm, 0, (int64_t)(tm.tm_year + 1900));
        set_reg_val(vm, 1, (int64_t)(tm.tm_mon + 1));
        set_reg_val(vm, 2, (int64_t)tm.tm_mday);
        set_reg_val(vm, 3, (int64_t)tm.tm_hour);
        set_reg_val(vm, 4, (int64_t)tm.tm_min);
        set_reg_val(vm, 5, (int64_t)tm.tm_sec);
        return true;
    }

    case K3_SYS_HOST_METRICS: {
        struct sysinfo si;
        (void)sysinfo(&si);
        int cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
        long total_mb = (long)((si.totalram * si.mem_unit) / (1024 * 1024));
        long free_mb = (long)((si.freeram * si.mem_unit) / (1024 * 1024));
        set_reg_val(vm, 0, (int64_t)cores);
        set_reg_val(vm, 1, (int64_t)total_mb);
        set_reg_val(vm, 2, (int64_t)free_mb);
        return true;
    }

    case K3_SYS_SYSINFO: {
        int64_t cmd_count = reg_val(vm, 0);

        struct utsname u;
        (void)uname(&u);

        struct sysinfo si;
        (void)sysinfo(&si);
        int cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
        long total_mb = (long)((si.totalram * si.mem_unit) / (1024 * 1024));

        init_time();
        uint64_t elapsed = host_now_ms() - g_boot_millis;
        uint32_t total_sec = (uint32_t)(elapsed / 1000);
        uint32_t hr = total_sec / 3600;
        uint32_t min = (total_sec % 3600) / 60;
        uint32_t sec = total_sec % 60;

        printf("K3-OS v0.2\n");
        printf("Architecture: Ternary\n");
        printf("Runtime: Native emulator\n");
        printf("Host: %s %s\n", u.sysname, u.machine);
        printf("CPU: %d threads\n", cores);
        printf("Memory: %ld MiB\n", total_mb);
        printf("Uptime: %02u:%02u:%02u\n", hr, min, sec);
        printf("Commands executed: %lld\n", (long long)cmd_count);
        fflush(stdout);
        return true;
    }

    case K3_SYS_HELP: {
        printf("K3-OS Commands:\n");
        printf("  h, help              Display this command guide\n");
        printf("  i, info              Show system, host & runtime telemetry\n");
        printf("  e, echo <text>       Echo message to console\n");
        printf("  p, peek <addr>       Inspect memory at <addr> [0..728]\n");
        printf("  m, mem <addr>        Dump 4 consecutive memory words\n");
        printf("  w, poke <a> <val>    Write decimal value to address <a>\n");
        printf("  c, cat <path>        Read and display host file\n");
        printf("  x, exec <path>       Load and execute an external .k3 program\n");
        printf("  r, rand              Generate random balanced-ternary word\n");
        printf("  g, gui               Launch Graphical Desktop Environment\n");
        printf("  l, cls               Clear terminal display\n");
        printf("  q, exit, quit        Shut down K3-OS\n");
        fflush(stdout);
        return true;
    }

    case K3_SYS_DRAW_VRAM: {
        int64_t base = reg_val(vm, 0);
        int64_t w = reg_val(vm, 1);
        int64_t h = reg_val(vm, 2);

        if (base < 0 || base > K3_VM_MAX_ADDRESS || w <= 0 || h <= 0) {
            set_reg_val(vm, 0, -1);
            return true;
        }

        for (int64_t y = 0; y < h; ++y) {
            for (int64_t x = 0; x < w; ++x) {
                int64_t addr = base + y * w + x;
                if (addr > K3_VM_MAX_ADDRESS) break;
                bt_word word;
                int64_t val = 0;
                if (k3_vm_get_mem(vm, (uint16_t)addr, &word)) {
                    (void)bt_word_to_i64(&word, &val);
                }
                if (val == 1) {
                    printf("\033[1;32m█\033[0m");
                } else if (val == -1) {
                    printf("\033[1;31m█\033[0m");
                } else if (val == 0) {
                    putchar(' ');
                } else if (val >= 32 && val <= 126) {
                    putchar((int)val);
                } else {
                    printf("\033[90m·\033[0m");
                }
            }
            putchar('\n');
        }
        fflush(stdout);
        set_reg_val(vm, 0, 0);
        return true;
    }

    case K3_SYS_GUI_DESKTOP: {
        bool interactive = isatty(STDIN_FILENO);
        struct termios old_t, raw_t;

        if (interactive) {
            tcgetattr(STDIN_FILENO, &old_t);
            raw_t = old_t;
            raw_t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSANOW, &raw_t);
            printf("\033[?1049h\033[?25l");
        }

        int active_tab = 0; /* 0: System, 1: Apps, 2: Memory, 3: Scope */
        int scope_mode = 0; /* 0: Waveform, 1: Memory Scope, 2: Noise */
        int mem_scroll = 0; /* Memory viewer offset 0..2180 */
        char app_msg[128] = "Press [A] Factorial, [B] Counter, [C] Overflow to execute.";
        int frame = 0;
        uint64_t prev_vm_insns = vm->instruction_count;
        int vm_cpu_activity = 0;

        for (;;) {
            struct utsname u;
            (void)uname(&u);
            struct sysinfo si;
            (void)sysinfo(&si);
            int cores = (int)sysconf(_SC_NPROCESSORS_ONLN);
            long total_mb = (long)((si.totalram * si.mem_unit) / (1024 * 1024));

            init_time();
            uint64_t elapsed = host_now_ms() - g_boot_millis;
            uint32_t total_sec = (uint32_t)(elapsed / 1000);
            uint32_t hr = total_sec / 3600;
            uint32_t min = (total_sec % 3600) / 60;
            uint32_t sec = total_sec % 60;

            uint64_t cur_vm_insns = vm->instruction_count;
            uint64_t d_insns = (cur_vm_insns >= prev_vm_insns) ? (cur_vm_insns - prev_vm_insns) : 0;
            prev_vm_insns = cur_vm_insns;

            if (d_insns > 0) {
                vm_cpu_activity = 100;
            } else {
                vm_cpu_activity = (vm_cpu_activity * 2) / 3;
                if (vm_cpu_activity < 0) vm_cpu_activity = 0;
            }

            int non_zero_words = 0;
            for (size_t mi = 0; mi < K3_VM_MEMORY_WORDS; ++mi) {
                for (size_t ti = 0; ti < K3_TRITS; ++ti) {
                    if (vm->memory[mi].t[ti] != 0) {
                        non_zero_words++;
                        break;
                    }
                }
            }
            int vm_mem_pct = (non_zero_words * 100) / K3_VM_MEMORY_WORDS;

            char vm_cpu_bar[128];
            char vm_mem_bar[128];
            render_bar(vm_cpu_bar, sizeof(vm_cpu_bar), vm_cpu_activity, 16);
            render_bar(vm_mem_bar, sizeof(vm_mem_bar), vm_mem_pct, 16);

            const char *cpu_col = (vm_cpu_activity > 70) ? "\033[1;31m" : ((vm_cpu_activity > 30) ? "\033[1;33m" : "\033[1;32m");
            const char *mem_col = (vm_mem_pct > 80) ? "\033[1;31m" : ((vm_mem_pct > 50) ? "\033[1;33m" : "\033[1;32m");

            if (interactive) {
                printf("\033[H");
            }

            /* Top Menu Bar */
            printf("\033[44;1;37m [ K3-OS Desktop ] \033[0m");
            printf("%s 1:System \033[0m", (active_tab == 0) ? "\033[47;30;1m" : "\033[40;37m");
            printf("%s 2:Apps \033[0m", (active_tab == 1) ? "\033[47;30;1m" : "\033[40;37m");
            printf("%s 3:Memory \033[0m", (active_tab == 2) ? "\033[47;30;1m" : "\033[40;37m");
            printf("%s 4:Scope \033[0m", (active_tab == 3) ? "\033[47;30;1m" : "\033[40;37m");
            printf("\033[44;1;37m ── %02u:%02u:%02u ── \033[0m\n\n", hr, min, sec);

            /* Main Content Area based on active_tab */
            if (active_tab == 0) {
                /* Tab 0: System Telemetry */
                printf("\033[1;36m┌──[ System Telemetry ]─────────────────────────────────────────┐\033[0m\n");
                printf("\033[1;36m│\033[0m  Architecture: \033[1;33mTernary (9-trit balanced)\033[0m                      \033[1;36m│\033[0m\n");
                printf("\033[1;36m│\033[0m  Memory Space: \033[1;32m%d words (3^7 addressing)\033[0m                      \033[1;36m│\033[0m\n", K3_VM_MEMORY_WORDS);
                printf("\033[1;36m│\033[0m  Host Kernel:  \033[1;37m%s %s (%d cores)\033[0m                        \033[1;36m│\033[0m\n", u.sysname, u.machine, cores);
                printf("\033[1;36m│\033[0m  Host RAM:     \033[1;37m%ld MiB Physical Memory\033[0m                         \033[1;36m│\033[0m\n", total_mb);
                printf("\033[1;36m│\033[0m  Uptime:       \033[1;35m%02u:%02u:%02u\033[0m                                         \033[1;36m│\033[0m\n", hr, min, sec);
                printf("\033[1;36m│\033[0m  VM Memory:    %s[%s] %3d%% (%d/%d words)\033[0m   \033[1;36m│\033[0m\n",
                       mem_col, vm_mem_bar, vm_mem_pct, non_zero_words, K3_VM_MEMORY_WORDS);
                printf("\033[1;36m│\033[0m  VM CPU Load:  %s[%s] %3d%% (%s)\033[0m               \033[1;36m│\033[0m\n",
                       cpu_col, vm_cpu_bar, vm_cpu_activity, (vm_cpu_activity > 0) ? "Active" : "Idle  ");
                printf("\033[1;36m└───────────────────────────────────────────────────────────────┘\033[0m\n\n");
            } else if (active_tab == 1) {
                /* Tab 1: Application Launcher */
                printf("\033[1;33m┌──[ K3-OS Application & Process Manager ]──────────────────────┐\033[0m\n");
                printf("\033[1;33m│\033[0m  Select an app to spawn & execute inside the VM:              \033[1;33m│\033[0m\n");
                printf("\033[1;33m│\033[0m    \033[1;32m[A]\033[0m factorial.k3      - Compute 7! (5040) loop algorithm   \033[1;33m│\033[0m\n");
                printf("\033[1;33m│\033[0m    \033[1;32m[B]\033[0m counter.k3        - Register countdown benchmark       \033[1;33m│\033[0m\n");
                printf("\033[1;33m│\033[0m    \033[1;32m[C]\033[0m overflow.k3       - Hardware Overflow Trap Demo        \033[1;33m│\033[0m\n");
                printf("\033[1;33m│\033[0m    \033[1;32m[D]\033[0m memtest.k3        - LOAD / STORE roundtrip test        \033[1;33m│\033[0m\n");
                printf("\033[1;33m│\033[0m    \033[1;32m[S]\033[0m snake.k3          - Play Real-Time Ternary Snake!      \033[1;33m│\033[0m\n");
                printf("\033[1;33m│\033[0m                                                               \033[1;33m│\033[0m\n");
                printf("\033[1;33m│\033[0m  Last Process Output:                                         \033[1;33m│\033[0m\n");
                printf("\033[1;33m│\033[0m    >> %-55s  \033[1;33m│\033[0m\n", app_msg);
                printf("\033[1;33m└───────────────────────────────────────────────────────────────┘\033[0m\n\n");
            } else if (active_tab == 2) {
                /* Tab 2: Memory Inspector */
                printf("\033[1;35m┌──[ Live Memory Inspector (Address %04d..%04d / %d) ]──────────┐\033[0m\n",
                       mem_scroll, mem_scroll + 7, K3_VM_MEMORY_WORDS);
                for (int i = 0; i < 8; ++i) {
                    uint16_t addr = (uint16_t)(mem_scroll + i);
                    bt_word w;
                    int64_t val = 0;
                    if (k3_vm_get_mem(vm, addr, &w)) {
                        (void)bt_word_to_i64(&w, &val);
                    }
                    printf("\033[1;35m│\033[0m  mem[%04u] = %6lld  [", addr, (long long)val);
                    for (size_t t = 0; t < K3_TRITS; ++t) {
                        if (w.t[t] > 0) printf("\033[1;32m+\033[0m");
                        else if (w.t[t] < 0) printf("\033[1;31m-\033[0m");
                        else printf("\033[90m0\033[0m");
                    }
                    printf("]                                \033[1;35m│\033[0m\n");
                }
                printf("\033[1;35m│\033[0m  \033[90m[J] Down (+8) │ [K] Up (-8) │ [0] Reset to 0 │ [T] Top (2000)\033[0m \033[1;35m│\033[0m\n");
                printf("\033[1;35m└───────────────────────────────────────────────────────────────┘\033[0m\n\n");
            } else {
                /* Tab 3: Visual Scope */
                const char *mode_name = (scope_mode == 0) ? "Harmonic Waveform" : ((scope_mode == 1) ? "Live Memory Scope" : "Trit Noise Matrix");
                printf("\033[1;32m┌──[ Visual Ternary Scope (%s) ]────────────────────────┐\033[0m\n", mode_name);

                if (scope_mode == 0) {
                    int vals[27];
                    for (int col = 0; col < 27; ++col) {
                        int s = g_sin32[(col * 2 + frame * 3) & 31] + (g_sin32[(col - frame + 8) & 31] / 2);
                        vals[col] = (s > 35) ? 1 : ((s < -35) ? -1 : 0);
                    }
                    for (int row = 1; row >= -1; --row) {
                        printf("\033[1;32m│\033[0m  %c ", row > 0 ? '+' : (row < 0 ? '-' : '0'));
                        for (int col = 0; col < 27; ++col) {
                            if (vals[col] == row) {
                                if (row > 0) printf("\033[1;32m█\033[0m ");
                                else if (row < 0) printf("\033[1;31m█\033[0m ");
                                else printf("\033[90m■\033[0m ");
                            } else {
                                printf("  ");
                            }
                        }
                        printf("  \033[1;32m│\033[0m\n");
                    }
                } else if (scope_mode == 1) {
                    for (int row = 1; row >= -1; --row) {
                        printf("\033[1;32m│\033[0m  %c ", row > 0 ? '+' : (row < 0 ? '-' : '0'));
                        for (int col = 0; col < 27; ++col) {
                            bt_word w;
                            int val = 0;
                            if (k3_vm_get_mem(vm, (uint16_t)((col + frame) % 27), &w)) {
                                val = w.t[(col + frame) % 9];
                            }
                            if (val == row) {
                                if (row > 0) printf("\033[1;32m█\033[0m ");
                                else if (row < 0) printf("\033[1;31m█\033[0m ");
                                else printf("\033[90m■\033[0m ");
                            } else {
                                printf("  ");
                            }
                        }
                        printf("  \033[1;32m│\033[0m\n");
                    }
                } else {
                    for (int r = 0; r < 3; ++r) {
                        printf("\033[1;32m│\033[0m  ");
                        for (int c = 0; c < 27; ++c) {
                            int t = (rand() % 3) - 1;
                            if (t > 0) printf("\033[1;32m+\033[0m ");
                            else if (t < 0) printf("\033[1;31m-\033[0m ");
                            else printf("\033[90m0\033[0m ");
                        }
                        printf(" \033[1;32m│\033[0m\n");
                    }
                }
                printf("\033[1;32m└───────────────────────────────────────────────────────────────┘\033[0m\n\n");
            }

            /* Status & Navigation Bar */
            printf("\033[47;30m [1-4] Switch Tab │ [A-D] Run App │ [J/K] Scroll Mem │ [Q/ESC] Return \033[0m\n");
            fflush(stdout);

            if (!interactive) {
                break;
            }

            /* Wait up to 200ms for user keystroke */
            struct timeval tv = { 0, 200000 };
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);
            int ready = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
            if (ready > 0) {
                char ch = 0;
                if (read(STDIN_FILENO, &ch, 1) == 1) {
                    if (ch == 'q' || ch == 'Q' || ch == 27) {
                        break;
                    } else if (ch >= '1' && ch <= '4') {
                        active_tab = ch - '1';
                    } else if (ch == '\t') {
                        active_tab = (active_tab + 1) % 4;
                    } else if (ch == ' ') {
                        if (active_tab == 3) scope_mode = (scope_mode + 1) % 3;
                    } else if (ch == 'r' || ch == 'R') {
                        frame += 3;
                    } else if (ch == 'j' || ch == 'J' || ch == '+') {
                        if (mem_scroll + 8 <= K3_VM_MAX_ADDRESS - 7) mem_scroll += 8;
                    } else if (ch == 'k' || ch == 'K' || ch == '-') {
                        if (mem_scroll >= 8) mem_scroll -= 8;
                        else mem_scroll = 0;
                    } else if (ch == '0') {
                        mem_scroll = 0;
                    } else if (ch == 't' || ch == 'T') {
                        mem_scroll = 2000;
                    } else if (ch == 'a' || ch == 'A') {
                        run_desktop_app("build/examples/factorial.k3", app_msg, sizeof(app_msg));
                        active_tab = 1;
                    } else if (ch == 'b' || ch == 'B') {
                        run_desktop_app("build/examples/counter.k3", app_msg, sizeof(app_msg));
                        active_tab = 1;
                    } else if (ch == 'c' || ch == 'C') {
                        run_desktop_app("build/examples/overflow.k3", app_msg, sizeof(app_msg));
                        active_tab = 1;
                    } else if (ch == 'd' || ch == 'D') {
                        run_desktop_app("build/examples/memtest.k3", app_msg, sizeof(app_msg));
                        active_tab = 1;
                    } else if (ch == 's' || ch == 'S') {
                        if (active_tab == 1) {
                            run_desktop_app("build/examples/snake.k3", app_msg, sizeof(app_msg));
                        }
                    }
                }
            }
            frame++;
        }

        if (interactive) {
            printf("\033[?25h\033[?1049l"); /* Restore main screen buffer and cursor */
            tcsetattr(STDIN_FILENO, TCSANOW, &old_t);
            fflush(stdout);
        }

        set_reg_val(vm, 0, 0);
        return true;
    }

    case K3_SYS_RAW_MODE: {
        int64_t enable = reg_val(vm, 0);
        set_raw_mode(enable != 0);
        return true;
    }

    default:
        /* Unhandled syscall: trigger VM explicit trap */
        return false;
    }
}
