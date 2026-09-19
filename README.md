# OS Sync — Custom Kernel Lock (xv6)

Group operating systems project extending **xv6-riscv** with a **custom synchronization lock** implemented in the kernel and exposed to user space through new system calls.

## What we built

A configurable **group-favoritism lock** (queue-based, sleep/wakeup) that:

- Blocks waiters in a FIFO queue protected by an internal spinlock
- On release, can preferentially wake a waiter with the same process group (`gid`) based on a favoritism percentage (0–100)
- Otherwise falls back to strict FIFO
- Is managed via syscalls: create, acquire, release, destroy

The project also includes helper syscalls for team scores and a multi-process **relay race** demo that stresses the lock under contention.

## Tech skills

- C
- Operating systems (xv6 kernel)
- Concurrency / synchronization
- System calls
- Process management (`fork`, `wait`, sleep/wakeup)
- RISC-V + QEMU
- Unix / Linux
- Make / Git

## Key files

| Area | Path |
|------|------|
| Lock + score syscalls | [`kernel/sysproc.c`](kernel/sysproc.c) |
| Syscall numbers / table | [`kernel/syscall.h`](kernel/syscall.h), [`kernel/syscall.c`](kernel/syscall.c) |
| User API | [`user/user.h`](user/user.h), [`user/usys.pl`](user/usys.pl) |
| Lock stress demo | [`user/relay_race.c`](user/relay_race.c) |
| Basic lock test | [`user/israeli_test.c`](user/israeli_test.c) |

## Build & run

Requires a RISC-V toolchain and QEMU (`riscv64-softmmu`).

```bash
make qemu
```

Inside xv6, run:

```text
relay_race 50
```

Favoritism argument: `0` (pure FIFO), `50` (default), or `100` (always prefer same-gid waiters when present).

## Credits

- Group coursework project (BGU Operating Systems)
- Based on [MIT xv6-riscv](https://pdos.csail.mit.edu/6.1810/) (see [`LICENSE`](LICENSE))
- Course base: BGU OS lab fork of xv6 with devcontainer support
