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
| one `burst_proc` loop pass (SPI read, decode, sinks, BLE flush) takes about 0.1 ms and stays under 1 ms | deep dive A3, chapter 7 section 2 | DWT | `M-09-4`: 323 us typical, 396 us longest at 240 Hz with the fusion sink; the 0.1 ms estimate was wrong, the 1 ms ceiling holds. 960 Hz pending |
| a `LOG_INF` in a handler costs "a few microseconds" (deferred mode) | chapter 5 section 6 | DWT | pending, chapter 5 |
| uncontended mutex lock plus unlock: hundreds of cycles; spinlock pair: tens | chapter 6 sections 2 and 3 | DWT | pending, chapter 6 |
| consumer hold time of the DSP slot, one 2 kB memcpy, about 30 us | chapter 7 section 5 | DWT | `M-07-3`: 289 us, about ten times the estimate |
| FFT 64 to 512 points: 0.5 to 3 ms, with the hardware FPU enabled | deep dive A3, chapter 7 section 7 | DWT | pending, chapter 11 |
| BLE flush of 14 notifications: about 2 ms | review F7, chapter 5 section 6 | DWT and sniffer | pending, chapter 12 |
| current per acquisition profile: 315 to 84 uA | ADR-001 table, chapter 9 | PPK2 | pending, chapters 9 and 14 |
| logging thread stack 768 B is enough | backlog open item | thread analyzer | pending, chapter 9 |
| one 9-axis filter step: tens of microseconds with the FPU, hundreds without; below 52 us (5 % of the CPU at 960 Hz) with the FPU | chapter 10 sections 2 and 7 | DWT | `M-10-1`: 12.5 us typical, 13.7 us longest, 9-axis with the FPU. 6-axis and the FPU-off runs pending |
| an extended Kalman filter costs several times the Madgwick step | chapter 10 section 2 | none, estimate only | not planned |
| the correction removes a 30 degree tilt error in 2.65 s at beta 0.1 and 8.0 s at beta 0.033 | chapter 10 section 4 | RTT stats line | pending, chapter 10 (`M-10-5`); host test checks the rate |
| gyro sensitivity 70 mdps per LSB at 2000 dps | chapter 9 section 6, chapter 10 | full turn on the bench, `turn z cdeg` in the stats line | pending, chapter 10 (`M-10-2`) |
| `burst_proc` needs below 1024 B of stack with 9-axis fusion | chapter 10 section 11 | thread analyzer | pending, chapter 10 (`M-10-3`) |
| one magnetometer poll is one I2C transaction of about 0.2 ms | chapter 10 One layer below | logic analyser on the DK | pending, chapter 10 (`M-10-4`) |
| a calibration save (flash write, possible page erase of about 85 ms) causes no FIFO overrun at 960 Hz | chapter 10 section 9 | counters in the stats line | pending, chapter 10 (`M-10-6`) |
| a background burst of 127 samples with fusion takes a few milliseconds | chapter 10 section 7 | DWT | pending, chapter 10 (`M-10-7`) |

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

### M-07-3  hold time of one DSP slot: memcpy of a full 512-float window
- Date, commit: 2026-11-15, 6bf5634 plus the uncommitted chapter 10 work tree (boot banner `v0.1.0-dev-6bf56344fbe3`)
- Build: `west build -p -d ../build_dbg -b imu_tag_v1/nrf52840 --sysbuild firmware -- "-DEXTRA_CONF_FILE=debug.conf"`; `CONFIG_FPU=y`, `CONFIG_FPU_SHARING=y`
- Instrument: DWT cycle counter around one `memcpy` of 2048 B from a static source to a static destination in the stats handler on `app_wq` (`stats.c`), read from the RTT log
- Setup: `imu_tag_v1` prototype at rest, flat on the table, no probes; 240 Hz configured, `ACQ_PROFILE_LIVE_REALTIME` (watermark 8 rows), fusion 9-axis at beta 0.1, calibration at defaults, sinks: fusion and log; no BLE, no DSP; nine stats ticks, 5 s to 45 s after boot
- Raw: eight ticks `18469 cycles, 288578 ns`, one tick (40 s) `30304 cycles, 473500 ns`
- Derived: 289 us uninterrupted, 9.0 cycles per byte, so the library copy in this build moves single bytes; that is 0.43 % of the 67 ms window period at 960 Hz. The 474 us tick is the copy preempted once on `app_wq` (priority 8), most likely by a burst pass of `burst_proc` (see `M-09-4`); that reading of the outlier is not proven
- Compared with: chapter 7 section 5 estimate "about 30 us": wrong by a factor of ten. The hold-time argument of chapter 7 has to be restated with 289 us
- Next: repeat when `dsp_worker` exists (chapter 11) with the copy on that thread; look at which `memcpy` the build links

### M-09-0  watermark unit: rows or pairs
- Date, commit: 2026-11-15, 6bf5634 plus the uncommitted chapter 10 work tree (boot banner `v0.1.0-dev-6bf56344fbe3`)
- Build: `west build -p -d ../build_dbg -b imu_tag_v1/nrf52840 --sysbuild firmware -- "-DEXTRA_CONF_FILE=debug.conf"`; `CONFIG_FPU=y`, `CONFIG_FPU_SHARING=y`
- Instrument: RTT log, `int1 isr`, `bursts` and `fifo: samples` counters from the stats lines
- Setup: `imu_tag_v1` prototype at rest, flat on the table, no probes; 240 Hz configured, `ACQ_PROFILE_LIVE_REALTIME` (watermark 8 rows), fusion 9-axis at beta 0.1, calibration at defaults, sinks: fusion and log; no BLE, no DSP; lines at 5 s and 45 s (40 s apart, not the 60 s the chapter asks for)
- Raw: bursts 293 to 2640, samples 1172 to 10560, ISR count equal to the burst count on every line; `level max 8`, overruns 0, orphans 0, unknown tags 0, bus errors 0, anchor fallbacks 0
- Derived: 2347 bursts and 9388 samples in 40 s: 4.000 samples per burst and 58.7 bursts per second. A watermark of 8 therefore counts FIFO rows, two rows per sample. The sample count is 2.2 % below 240 per second times 40 s; that is the sensor rate (`M-09-8`), not a wrong batch rate, which would be a factor of two
- Compared with: chapter 9 task 5 (3600 bursts per 60 s for rows, 1800 for pairs) and the sensor binding
- Next: none for the unit. `CLAUDE.md` rule 10 and `docs/adr/ADR-001.md` take the fact and the burst period of 17.0 ms

### M-09-4  run time of one burst loop pass, 240 Hz, with the fusion sink
- Date, commit: 2026-11-15, 6bf5634 plus the uncommitted chapter 10 work tree (boot banner `v0.1.0-dev-6bf56344fbe3`)
- Build: `west build -p -d ../build_dbg -b imu_tag_v1/nrf52840 --sysbuild firmware -- "-DEXTRA_CONF_FILE=debug.conf"`; `CONFIG_FPU=y`, `CONFIG_FPU_SHARING=y`
- Instrument: DWT cycle counter from the return of `imu_fifo_wait()` to the next call (`burst_processor.c`), last and longest pass in the stats line
- Setup: `imu_tag_v1` prototype at rest, flat on the table, no probes; 240 Hz configured, `ACQ_PROFILE_LIVE_REALTIME` (watermark 8 rows), fusion 9-axis at beta 0.1, calibration at defaults, sinks: fusion and log; no BLE, no DSP; nine stats ticks, 5 s to 45 s
- Raw: last pass 322812 to 343281 ns (eight of nine lines between 322812 and 323703); longest pass 363875 ns in the first 5 s, 396312 ns from 20 s on
- Derived: 323 us typical and 396 us longest for one pass of 8 rows: status read, row read, decode, two sinks for 4 samples. Four filter steps of `M-10-1` are about 50 us of it. The pass is 1.9 % of the 17.0 ms burst period
- Compared with: deep dive A3 estimate of 0.1 ms: wrong by a factor of three. The 1 ms ceiling of chapter 7 section 2 holds
- Next: 960 Hz run, and the split of the pass into bus time and CPU time (`M-09-2` on the DK); `M-10-7` for the background profile

### M-09-6  thread anchor against ISR anchor, run (a): 240 Hz at rest
- Date, commit: 2026-11-15, 6bf5634 plus the uncommitted chapter 10 work tree (boot banner `v0.1.0-dev-6bf56344fbe3`)
- Build: `west build -p -d ../build_dbg -b imu_tag_v1/nrf52840 --sysbuild firmware -- "-DEXTRA_CONF_FILE=debug.conf"`; `CONFIG_FPU=y`, `CONFIG_FPU_SHARING=y`
- Instrument: `D` per burst in `imu_fifo_do_burst()` under `CONFIG_IMU_TAG_DEBUG_COUNTERS`, minimum, maximum, mean and nine-bin histogram printed and cleared by the stats item
- Setup: `imu_tag_v1` prototype at rest, flat on the table, no probes; 240 Hz configured, `ACQ_PROFILE_LIVE_REALTIME` (watermark 8 rows), fusion 9-axis at beta 0.1, calibration at defaults, sinks: fusion and log; no BLE, no DSP; nine stats ticks, 5 s to 45 s, 2640 bursts
- Raw: minimum 2 ticks on every line, maximum 3 to 5 ticks, mean 2.87 to 2.92 ticks; histogram summed over the nine lines: 2629 bursts in the bin below 4 ticks, 11 in the bin below 8, none elsewhere
- Derived: spread 3 ticks, which is 92 us or 2.2 % of the nominal sample period of 4167 us. The thread anchor is late by 2.9 ticks (88 us) on average
- Compared with: chapter 9 task 7 expectation for a run at rest: every burst below 8 ticks. Met
- Next: runs (b), (c) and (d) at 960 Hz

### M-09-8  sensor sample rate against the MCU clock, first run of 40 s
- Date, commit: 2026-11-15, 6bf5634 plus the uncommitted chapter 10 work tree (boot banner `v0.1.0-dev-6bf56344fbe3`)
- Build: `west build -p -d ../build_dbg -b imu_tag_v1/nrf52840 --sysbuild firmware -- "-DEXTRA_CONF_FILE=debug.conf"`; `CONFIG_FPU=y`, `CONFIG_FPU_SHARING=y`
- Instrument: sample counter and `k_uptime_ticks()` printed together in the stats line; system clock 32768 Hz from the low-frequency crystal (`CONFIG_CLOCK_CONTROL_NRF_K32SRC_XTAL=y`, 50 ppm class)
- Setup: `imu_tag_v1` prototype at rest, flat on the table, no probes; 240 Hz configured, `ACQ_PROFILE_LIVE_REALTIME` (watermark 8 rows), fusion 9-axis at beta 0.1, calibration at defaults, sinks: fusion and log; no BLE, no DSP; lines at 5 s and 45 s. The chapter asks for 600 s and for the value of `lsm6dsv16x_odr_cal_reg_get()`; neither is in this run
- Raw: samples 1172 at tick 165977, samples 10560 at tick 1476862
- Derived: 9388 samples in 1310885 ticks: 9388 x 32768 / 1310885 = 234.67 samples per second, 2.22 % below the nominal 240. The counter moves in steps of 4, so the figure is good to about 0.05 % over 40 s. With dt taken as 1/240 s the filter integrates 2.2 % too little angle, and the timestamp at the far end of a 127-sample burst is off by 0.0222 x 126 x 4167 us = 11.7 ms
- Compared with: chapter 9 task 7 threshold of 100 us for the far-end error: exceeded by two orders of magnitude. Backlog item "sample rate against nominal" holds the decision
- Next: the 600 s run with the calibration register read next to it, as a new entry; before `M-10-2`

### M-10-1  one filter step, 9-axis, FPU on
- Date, commit: 2026-11-15, 6bf5634 plus the uncommitted chapter 10 work tree (boot banner `v0.1.0-dev-6bf56344fbe3`)
- Build: `west build -p -d ../build_dbg -b imu_tag_v1/nrf52840 --sysbuild firmware -- "-DEXTRA_CONF_FILE=debug.conf"`; `CONFIG_FPU=y`, `CONFIG_FPU_SHARING=y`
- Instrument: DWT cycle counter around `orientation_service_update()` in `fusion_sink.c`; shortest, last and longest step per stats interval. `arm-zephyr-eabi-objdump -d --disassemble=madgwick_update_6axis` on the default build of the same tree
- Setup: `imu_tag_v1` prototype at rest, flat on the table, no probes; 240 Hz configured, `ACQ_PROFILE_LIVE_REALTIME` (watermark 8 rows), fusion 9-axis at beta 0.1, calibration at defaults, sinks: fusion and log; no BLE, no DSP; flags 0x03 on every line, so the published step was a 9-axis step each time and `mag rejected` stayed 0; nine stats ticks, 5 s to 45 s, 10560 steps
- Raw: last 12453 ns on every line; longest 13718 ns on every line; shortest 12453 ns on eight lines and 0 ns on the first line. Disassembly of the 6-axis function: 3 `bl`, all to `inv_sqrt`
- Derived: 12.5 us (797 cycles) typical and 13.7 us (878 cycles) longest per 9-axis step and publish. At 960 Hz that is 1.2 % of the CPU, 1.3 % at the longest. The single 0 ns reading in the first interval is unexplained and is not counted as a step time
- Compared with: R2 of chapter 10 (52 us, 5 % of the CPU at 960 Hz): met with a factor of four to spare. The estimate "tens of microseconds" was high
- Next: the three missing runs as a new entry: 6-axis with the FPU, and both modes with `CONFIG_FPU=n`, with the `bl` count of that build; find the cause of the 0 ns reading

### M-10-3  stack use per thread with 9-axis fusion, first run at 240 Hz
- Date, commit: 2026-11-15, 6bf5634 plus the uncommitted chapter 10 work tree (boot banner `v0.1.0-dev-6bf56344fbe3`)
- Build: `west build -p -d ../build_dbg -b imu_tag_v1/nrf52840 --sysbuild firmware -- "-DEXTRA_CONF_FILE=debug.conf"`; `CONFIG_FPU=y`, `CONFIG_FPU_SHARING=y`
- Instrument: thread analyzer, `thread_analyzer_print(0)` 5 s after boot
- Setup: `imu_tag_v1` prototype at rest, flat on the table, no probes; 240 Hz configured, `ACQ_PROFILE_LIVE_REALTIME` (watermark 8 rows), fusion 9-axis at beta 0.1, calibration at defaults, sinks: fusion and log; no BLE, no DSP; no calibration save before the print. The chapter asks for 960 Hz after 60 s with a save in between; this run is the 240 Hz baseline
- Raw (used / size): `burst_proc` 576 / 2048, `app_wq` 624 / 2048, `sysworkq` 288 / 1024, `logging` 580 / 2048 (overlay size), `idle` 64 / 320, `main` 624 / 1024, `ISR0` 432 / 2048
- Derived: `burst_proc` 28 %, `app_wq` 30 %, `main` 61 %, `ISR0` 21 %. Against `M-04-2`: `burst_proc` +320 B (decoder, sinks, float locals of the filter, FPU frame), `main` +24 B, `ISR0` +216 B
- Compared with: `M-04-2`; chapter 10 expectation for `burst_proc`: several hundred bytes above 256 and below 1024. Met
- Next: the run the chapter defines (960 Hz, 60 s, a calibration save on `app_wq`) as a new entry, then size `BURST_PROCESSOR_STACK` by the chapter 3 rule
