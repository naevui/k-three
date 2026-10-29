# AGENTS.md - K-Three (Kord-Three) AI Agent Context & Rules

This project uses `CLAUDE.md` as its primary reference for AI coding agents. Please refer to [`CLAUDE.md`](CLAUDE.md) for full instructions, including:

1. **System & Architecture:** 9-trit balanced ternary, symmetric range `[-9841 .. +9841]`, $3^7 = 2187$ words memory.
2. **Build & Test Commands:** `make`, `make test`, `make debug` (ASan/UBSan clean).
3. **ISA v0 Reference:** All 27 opcodes (0..26), register layout `r0..r7`, `ERET`, `SETVEC`.
4. **Syscall ABI:** Codes 0..35, register calling conventions (`r0..r2`).
5. **Coding Rules:** Strict C11, zero warnings, zero external libraries, 4-trit immediate bounds `[-40..+40]`.

See [`CLAUDE.md`](CLAUDE.md) for the complete reference.
