# OS Sync — Custom Kernel Lock (xv6)

Group operating systems project extending **xv6-riscv** with a **custom group-favoritism lock** in the kernel, exposed through new system calls. The lock blends FIFO waiting with a configurable amount of preference for processes that share a group id (`gid`) — useful for studying fairness vs. preferential scheduling under contention.

## Problem & goals

Standard locks are typically fair (FIFO) or opaque about who wakes next. This project implements a synchronization policy where processes that “know each other” (same `gid`) may be preferred on release, controlled by a favoritism percentage (0–100). Goals:

- Extend the xv6 kernel with process groups and a new lock type
- Avoid busy-waiting (use sleep/wakeup)
- Observe how lock policy changes outcomes in a concurrent multi-process workload

## What we implemented

**Phase 0 — Kernel PRNG**

- Linear congruential generator (`lcg_srand` / `lcg_rand`) with shared state protected by a spinlock
- User-space access via system calls

**Phase 1 — Custom lock + process groups**

- `gid` field on `struct proc`, plus `setgid` / `getgid`
- Kernel lock with create / acquire / release / destroy syscalls
- Waiters in a FIFO queue; on release, with probability *c*% prefer the earliest waiter with the same `gid` as the releaser, otherwise wake FIFO head
- Internal spinlock for metadata; blocking via sleep/wakeup (no busy-wait)

**Phase 2 — Relay race demo**

- User program: multiple teams of runners share one lock (the “baton”)
- Same team ⇒ same `gid`; scoring under lock contention shows how favoritism affects winners

## Design highlights

- Queue-based waiters (fixed capacity); favoritism only among processes already queued
- Separation of concerns: spinlock protects lock state; sleep/wakeup blocks waiters
- Explicit fairness knob: `0` = pure FIFO, `100` = always prefer same-`gid` when present
- High favoritism can amplify group dominance (starvation risk for other groups under sustained same-`gid` traffic)

## Demo & observations

Build and run with QEMU, then:

```text
relay_race 0    # pure FIFO — scores tend to be more balanced
relay_race 50   # mixed policy (default)
relay_race 100  # strong same-team preference — one team often pulls ahead
```

Use these runs to compare how synchronization policy shapes concurrent outcomes, not just correctness.

## Tech skills

- C
- Operating systems (xv6 kernel)
- Concurrency / synchronization
- System calls
- Process management (`fork`, `wait`, sleep/wakeup)
- Fairness vs. priority-style lock policies
- PRNG / LCG
- RISC-V + QEMU
- Unix / Linux
- Make / Git

## Key files

| Area | Path |
|------|------|
| Lock + score + PRNG syscalls | [`kernel/sysproc.c`](kernel/sysproc.c) |
| Syscall numbers / table | [`kernel/syscall.h`](kernel/syscall.h), [`kernel/syscall.c`](kernel/syscall.c) |
| User API | [`user/user.h`](user/user.h), [`user/usys.pl`](user/usys.pl) |
| Lock stress demo | [`user/relay_race.c`](user/relay_race.c) |
| Basic lock test | [`user/israeli_test.c`](user/israeli_test.c) |

## Build & run

Requires a RISC-V toolchain and QEMU (`riscv64-softmmu`).

```bash
make qemu
```

Inside xv6:

```text
relay_race 50
```

## Credits

- Pair/group coursework — BGU Operating Systems (202.1.3031), Assignment 2: Synchronization
- Based on [MIT xv6-riscv](https://pdos.csail.mit.edu/6.1810/) (see [`LICENSE`](LICENSE))
- Course base: BGU OS lab fork of xv6 with devcontainer support
