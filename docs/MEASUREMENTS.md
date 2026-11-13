# Measurements

Every number this firmware relies on is either measured here or marked as
an estimate that still has to be. One entry per measurement, never edited
after the fact: a repeat is a new entry that points at the old one. The
wiki chapters say *what* to measure; this page is the only place the
*result* lives. `docs/BACKLOG.md` keeps decisions and open items, not
figures.

## How an entry is written

```
### M-<chapter>-<n>  <one line: what was measured>
- Date, commit: <YYYY-MM-DD>, <short hash of the firmware under test>
- Build: <exact west command, overlays included>
- Instrument: <tool and the setting that matters: sample rate, mode, probe>
- Setup: <board state: profile, connection, what is running, probe points>
- Raw: <the figure as the instrument showed it, with unit>
- Derived: <what was computed from it, with the formula>
- Compared with: <the budget, estimate or earlier entry it is held against>
- Next: <when it is repeated, or "none">
```

Raw and Derived are separate on purpose: the raw figure is the evidence, the
derived figure is the claim.

## Instruments on the bench

| Instrument | What it measures here | Chapters |
| --- | --- | --- |
| thread analyzer (`debug.conf` overlay) | stack use per thread, run time share | 3, 5, 11 |
| `west build -t ram_report` / `rom_report` | static RAM and flash per symbol | 2, 11 |
| DWT cycle counter (`CONFIG_TIMING_FUNCTIONS=y` in `debug.conf`) | durations inside the firmware at 64 MHz resolution | 5, 6, 7, 11 |
| logic analyser with SPI decoder | INT1 to chip-select latency, burst duration, bytes on the wire | 4, 8, 9, 13 |
| BLE sniffer (nRF Sniffer for Bluetooth LE in Wireshark, or equivalent) | connection interval, notifications per event, MTU, pairing | 12, 15 |
| Power Profiler Kit II | average and peak current per acquisition profile and power tier | 8, 9, 14 |
| `arm-zephyr-eabi-objdump -d` | what the compiler made of a critical section | 3, 6, 7 |

Probe points on `imu_tag_v1` rev 1.1.0 (board file and pinctrl): INT1
P0.30, INT2 P0.08, SPI1 CS P0.05, SCK P0.02, MOSI P0.29, MISO P0.31.

## Assertions to retire

Numbers the wiki states as estimates or from documentation. Each one is
replaced by an entry below; until then it is an assumption.

| Claim | Where stated | Instrument | Entry |
| --- | --- | --- | --- |
| INT1 rising edge to first SPI byte "well under 50 us" | chapter 4 section 7 | logic analyser | pending, chapter 9 |
| one FIFO burst of 254 rows takes about 1.8 ms at 8 MHz | chapter 8 section 6, review section 2 | logic analyser | pending, chapter 9 |
| one `burst_proc` loop pass (SPI read, decode, sinks, BLE flush) takes about 0.1 ms and stays under 1 ms | deep dive A3, chapter 7 section 2 | DWT | pending, chapter 9 (`M-09-4`) |
| a `LOG_INF` in a handler costs "a few microseconds" (deferred mode) | chapter 5 section 6 | DWT | pending, chapter 5 |
| uncontended mutex lock plus unlock: hundreds of cycles; spinlock pair: tens | chapter 6 sections 2 and 3 | DWT | pending, chapter 6 |
| consumer hold time of the DSP slot, one 2 kB memcpy, about 30 us | chapter 7 section 5 | DWT | pending, chapter 7 (`M-07-3`, hook in `stats.c`) |
| FFT 64 to 512 points: 0.5 to 3 ms, with the hardware FPU enabled | deep dive A3, chapter 7 section 7 | DWT | pending, chapter 11 |
| BLE flush of 14 notifications: about 2 ms | review F7, chapter 5 section 6 | DWT and sniffer | pending, chapter 12 |
| current per acquisition profile: 315 to 84 uA | ADR-001 table, chapter 9 | PPK2 | pending, chapters 9 and 14 |
| logging thread stack 768 B is enough | backlog open item | thread analyzer | pending, chapter 9 |

## Entries

### M-02-1  RAM and flash after chapter 2
- Date, commit: 2026-11-06, chapter 2 commit
- Build: `west build -d build/firmware -t ram_report` (application image only)
- Instrument: ram_report
- Setup: empty application, boot log only
- Raw: RAM 9584 B of 256 KB (3.66 %); flash 37252 B of 499200 B (7.46 %)
- Derived: largest RAM items: interrupt stack 2048 B, RTT up buffer 1024 B, main stack 1024 B, CryptoCell platform context 448 B
- Compared with: nothing, first baseline
- Next: M-02-2

### M-02-2  RAM and flash after the chapter 2b stubs
- Date, commit: 2026-11-06, chapter 2b commit
- Build: same as M-02-1
- Instrument: ram_report
- Setup: three stub modules with their kernel objects
- Raw: RAM 10776 B (4.21 %); flash 37976 B
- Derived: +1192 B RAM. The first work item makes the linker keep the system work queue thread: 1024 B stack plus thread and queue structs
- Compared with: M-02-1
- Next: after chapter 11

### M-03-1  stack use per thread after step 2
- Date, commit: 2026-11-07, 2ce8c02
- Build: `west build -p -b imu_tag_v1/nrf52840 --sysbuild firmware -- "-DEXTRA_CONF_FILE=debug.conf"`
- Instrument: thread analyzer, `thread_analyzer_print(0)` 5 s after boot
- Setup: no FIFO, no BLE, no DSP; `burst_proc` blocked on its semaphore
- Raw (used / size): `burst_proc` 264 / 1024, `sysworkq` 288 / 1024, `logging` 528 / 2048 (overlay size; 768 in the default build), `idle` 64 / 320, `main` 584 / 1024, `ISR0` 216 / 2048
- Derived: `burst_proc` 25 %, `sysworkq` 28 %, `logging` 25 % of the overlay size and 68 % of the default 768, `main` 57 %, `ISR0` 10 %. The 264 B of `burst_proc` are its one `LOG_INF`; the blocked loop itself uses next to nothing
- Compared with: chapter 3 sizing rule, worst case plus 25 % rounded up to 256
- Next: after chapter 9 (FIFO) and chapter 11 (DSP), then size every stack from the figures

### M-04-1  INT1 plumbing armed and silent, 60 s, firmware-only
- Date, commit: 2026-11-08, 90716d8
- Build: `west build -p -b imu_tag_v1/nrf52840 --sysbuild firmware -- "-DEXTRA_CONF_FILE=debug.conf"`
- Instrument: RTT log (`CONFIG_LOG_BACKEND_RTT`, block mode); `s_isr_count` and `s_burst_count` read with `atomic_get` every 5 s from `main` under `CONFIG_IMU_TAG_DEBUG_COUNTERS` (`firmware/Kconfig`, set in `debug.conf`)
- Setup: `imu_tag_v1` prototype; sensor driver not configured (FIFO off, chapter 9 pending); INT1 P0.30 configured as input with `GPIO_INT_EDGE_TO_ACTIVE`; no probes (the prototype has no test points; wire-level traces are taken on the nRF52840 DK with the sensor breakout boards from chapter 9 on); Board at rest on table.
- Raw: `imu_fifo_init returned 0` at 0.9 ms after boot; twelve counter lines at 5 s spacing, 10 s to 60 s, all `int1 isr 0, bursts 0`. A first capture lost the analyzer tail and the 10 s line because the RTT viewer was not draining the 1024-byte up buffer (block mode, 4 retries of 5 ms); reconnecting the viewer before reset gave the complete log used here
- Derived: GPIOTE channel taken and ISR registered; no INT1 edge in 60 s with the pin as plain input and the sensor unconfigured
- Compared with: chapter 4 task 4 expectation (both counters 0)
- Next: M-09-1 on the nRF52840 DK with the LSM6DSV16X breakout, logic analyser on INT1, CS, SCK

### M-04-2  stack use per thread after the chapter 4 ISR path
- Date, commit: 2026-11-08, 90716d8
- Build: same as M-04-1
- Instrument: thread analyzer, `thread_analyzer_print(0)` 5 s after boot
- Setup: same image and board as M-04-1; `burst_proc` blocked in `imu_fifo_wait()`, no FIFO, no BLE, no DSP
- Raw (used / size): `burst_proc` 256 / 1024, `sysworkq` 288 / 1024, `logging` 528 / 2048 (overlay size), `idle` 64 / 320, `main` 600 / 1024, `ISR0` 216 / 2048
- Derived: against M-03-1, `burst_proc` -8 B (the loop blocks in `imu_fifo_wait()` instead of on its own semaphore) and `main` +16 B (the `imu_fifo_init()` call and its log line); every other thread unchanged
- Compared with: M-03-1
- Next: after chapter 9 (FIFO) and chapter 11 (DSP), then size every stack from the figures

### M-07-1  host tests of the DSP window hand-off, consumer lag case
- Date, commit: 2026-11-13, chapter 7 commit
- Build: in WSL (Ubuntu 22.04), from `/mnt/c/private/ws/imu-tag-showcase`: `nrfutil sdk-manager toolchain launch --ncs-version v3.3.0 -- west twister -p native_sim -T firmware/tests -O ~/twister-out`
- Instrument: twister on `native_sim`, ztest
- Setup: suites `imu_tag.app_config` (3 cases) and `imu_tag.dsp_accumulator` (4 cases); window length 8 samples; producer and consumer on the test thread, lag produced by not calling `dsp_accumulator_wait_window()`
- Raw: 2 of 2 test configurations passed, 7 of 7 test cases passed, 0 filtered, 0 failed, 253.58 s. The same command in Windows PowerShell filters both configurations and executes 0 cases
- Derived: with three windows pushed and none taken, two windows are delivered in order (`seq` 1, 2), exactly 1 window is dropped and counted once, the next window is `seq` 3, and the semaphore count ends at 0. The wedge of review F2 does not occur
- Compared with: chapter 7 section 4 (private design: second take returns the same slot again, producer blocked for good)
- Next: repeated by CI on every push (chapter 17)

### M-07-2  ordering of the stores before the READY publication in dsp_accumulator_push
- Date, commit: 2026-11-13, chapter 7 commit
- Build: `west build -d build` (default configuration), object `build/firmware/CMakeFiles/app.dir/src/dsp/dsp_accumulator.c.obj`
- Instrument: `arm-zephyr-eabi-objdump -d -r --disassemble=dsp_accumulator_push` and `--disassemble=atomic_set.isra.0` on the object file. The linked `zephyr.elf` does not contain the function yet: nothing calls it, so the linker discards its section
- Setup: none, static inspection
- Raw: in `dsp_accumulator_push`: `strh` to `s_len` at offset 0x6c, `str` to `s_seq` at 0x7e, `bl atomic_set.isra.0` at 0x84, `bl z_impl_k_sem_give` at 0x8a. `atomic_set.isra.0` is `dmb ish`, `ldrex`, `strex`, `cmp`, `bne`, `dmb ish`, `bx lr`
- Derived: the length and sequence stores precede the READY store, and a `dmb` sits between them and the exclusive store, as chapter 7 section 3 requires. The compiler did not inline `atomic_set()` here but emitted one shared copy for the three call sites in the file, so the barrier is inside the callee, not in the body of `push`
- Compared with: chapter 7 "One layer below" (expected the `dmb` inline in `push`)
- Next: repeat on `zephyr.elf` after chapter 11, when `dsp_worker` and the DSP sink call the module
