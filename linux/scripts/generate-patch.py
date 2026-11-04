#!/usr/bin/env python3
"""
linux/scripts/generate-patch.py

Generates the official upstream Linux kernel patch for the K-Three
balanced-ternary architecture (arch/k3/).
"""

import os
import sys
from datetime import datetime

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
ARCH_K3_DIR = os.path.join(ROOT, "linux", "arch", "k3")
OUT_PATCH = os.path.join(ROOT, "linux", "patches", "0001-arch-k3-Add-support-for-K-Three-balanced-ternary-architecture.patch")

COMMIT_MSG = """From: naevui <deniz@neutronyazilim.com.tr>
Date: Wed, 4 Nov 2026 00:00:00 +0300
Subject: [PATCH] arch/k3: Add support for K-Three 9-trit balanced-ternary architecture

This patch introduces the official Linux architecture port for K-Three (k3),
the world's first 9-trit balanced-ternary RISC processor.

Key architectural features of K-Three:
- Arithmetic: Base-3 balanced ternary (-1, 0, +1 trits)
- Word format: 9 trits per word, symmetric numerical range [-9841 .. +9841]
- Memory architecture: Flat word-addressed physical memory (6561 words, 9 pages of 729)
- Registers: 8 general-purpose 9-trit registers (r0..r7)
- Hardware features:
  - Hardware Memory-Mapped I/O (MMIO at base 6000: UART, Timer, RNG, LEDs)
  - Hardware block storage (disk) controller with DMA transfer
  - Hardware asynchronous timer interrupt (K3_IRQ_TIMER) and CPU IRQ lines
  - Hardware memory protection bounds (user_base, user_limit) for kernel/user isolation
  - Kernel trap vector (SETVEC) and exception return (ERET)

Architecture layer implementation:
- arch/k3/Kconfig: Architecture configuration options
- arch/k3/Makefile: Kbuild build rules
- arch/k3/kernel/vmlinux.lds.S: Memory layout and section definitions
- arch/k3/kernel/sched.c: Preemptive round-robin scheduler and task switching
- arch/k3/kernel/sys_k3.c: Linux system call dispatcher
- arch/k3/kernel/irq.c: Hardware interrupt registration and dispatch
- arch/k3/kernel/traps.c: Hardware trap to POSIX signal mapping
- arch/k3/kernel/init_task.c: Initial swapper task context
- arch/k3/fs/procfs.c: Virtual /proc filesystem (/proc/cpuinfo, /proc/meminfo)
- arch/k3/include/asm/: Architecture headers (atomic, barrier, bitops, io, irq,
  page, processor, ptrace, string, thread_info, uaccess, unistd)

Signed-off-by: naevui <deniz@neutronyazilim.com.tr>
---
"""

def generate_patch():
    file_entries = []
    
    # 1. arch/Kconfig entry
    kconfig_diff = """diff --git a/arch/Kconfig b/arch/Kconfig
index 0000000..1111111 100644
--- a/arch/Kconfig
+++ b/arch/Kconfig
@@ -10,3 +10,5 @@
 source "arch/$SRCARCH/Kconfig"
+source "arch/k3/Kconfig"
+
"""
    file_entries.append(("arch/Kconfig", 2, kconfig_diff))

    # 2. Walk linux/arch/k3/
    for root, dirs, files in os.walk(ARCH_K3_DIR):
        dirs.sort()
        for f in sorted(files):
            full_path = os.path.join(root, f)
            rel_path = os.path.relpath(full_path, os.path.join(ROOT, "linux"))
            
            with open(full_path, "r", encoding="utf-8", errors="replace") as fp:
                lines = fp.readlines()
            
            line_count = len(lines)
            
            diff_lines = [
                f"diff --git a/{rel_path} b/{rel_path}\n",
                "new file mode 100644\n",
                "index 0000000..1111111\n",
                "--- /dev/null\n",
                f"+++ b/{rel_path}\n",
                f"@@ -0,0 +1,{line_count} @@\n"
            ]
            for l in lines:
                diff_lines.append("+" + l if not l.endswith("\n") else "+" + l)
            if lines and not lines[-1].endswith("\n"):
                diff_lines.append("\n")
                
            file_entries.append((rel_path, line_count, "".join(diff_lines)))

    # Generate diffstat
    diffstat = []
    total_added = 0
    max_len = max(len(path) for path, _, _ in file_entries) + 2
    
    for path, count, _ in file_entries:
        total_added += count
        diffstat.append(f" {path.ljust(max_len)} | {str(count).rjust(4)} +")
    
    summary = f" {len(file_entries)} files changed, {total_added} insertions(+)\n\n"
    
    with open(OUT_PATCH, "w", encoding="utf-8") as out:
        out.write(COMMIT_MSG)
        out.write("\n".join(diffstat) + "\n")
        out.write(summary)
        for _, _, diff in file_entries:
            out.write(diff)
        out.write("-- \n2.47.0\n")

    print(f"Generated patch: {OUT_PATCH} ({len(file_entries)} files, {total_added} insertions)")

if __name__ == "__main__":
    generate_patch()
