# Backlog

Open items that are not tracked as TODO comments in code (see `CLAUDE.md`).

## Release blockers

Must be closed before any image leaves the bench. See the wiki README
section "Release blockers".

- static partition map (chapter 16): the Partition Manager lays out
  internal flash dynamically and the map can move between builds; Nordic
  requires a static map for DFU. Generate `firmware/pm_static.yml` from
  the last lab `build/partitions.yml` and commit it.
- production signing key (chapter 16): MCUboot still verifies against the
  MCUboot development key (`SB_CONFIG_BOOT_SIGNATURE_KEY_FILE` unset).
- image confirmation after FOTA (chapter 16): the application must confirm
  a new image, otherwise MCUboot reverts it on the next reset.

## Open

- measure sleep current with QSPI driver off (chapter 14): the MX25R6435F
  node stays because the chip exists; without `CONFIG_NORDIC_QSPI_NOR`
  nothing puts it into deep power-down.
- prj.conf console lines (chapter 2): `CONFIG_RTT_CONSOLE=y` and
  `CONFIG_STDOUT_CONSOLE=y` are assigned but end up `n` because
  `CONFIG_CONSOLE` is not enabled (build warning). The boot line arrives
  through `CONFIG_LOG_BACKEND_RTT`, not the console. Decide whether the
  console lines go or `CONFIG_CONSOLE=y` comes.
- logging thread stack (chapter 3): 528 of the default 768 bytes used
  (68 %) after two short boot messages, measured with the thread analyzer
  on 2026-11-07. One longer format string from any module can overflow
  it, and without `CONFIG_HW_STACK_PROTECTION` an overflow corrupts memory
  silently (the first analyzer run did exactly that). Decide between
  `CONFIG_LOG_PROCESS_THREAD_STACK_SIZE=1024` in `prj.conf` and
  `CONFIG_HW_STACK_PROTECTION=y`; measure again after chapter 9.
- INT1 counters at 240 Hz and 960 Hz (chapter 4 task 4): pending chapter 9,
  when the FIFO is configured; `s_isr_count` and `s_burst_count` must track
  each other within one.
- hardware FPU (chapters 10 and 11): `CONFIG_FPU` and `CONFIG_FPU_SHARING`
  are on since chapter 10 and `burst_proc` is created with `K_FP_REGS`.
  Open: add `CONFIG_CMSIS_DSP` with the FFT (chapter 11), re-measure the
  thread stacks on the bench, and name the FPU setting in every timing
  entry.
- partition map moved (chapters 10 and 16): enabling settings added the
  Partition Manager partition `settings_storage` at the end of internal
  flash and shrank both image slots by one page each. Expected, and the
  reason the map must be frozen in `firmware/pm_static.yml` before any
  release (release blocker above).
- sample rate against nominal (chapters 9 and 10): decided. The sensor
  delivers fewer samples per second than the configured rate, and its
  rate calibration register agrees with the counted rate on the
  prototype. `imu_ext_rate_mhz()` returns the corrected rate; the fusion
  time step and the decoder's timestamp period use it, and `CLAUDE.md`
  rule 9 says so. Open: the 600 s run of `M-09-8` as its own entry, then
  the gyro scale check `M-10-2`; chapters 11 and 12 must take their
  frequency axis and any rate they report from the same function.
- first samples after the FIFO start (chapters 9 and 10): decided. Two
  runs with the tag untouched show a z turn from the first samples after
  the sensor leaves power-down; a rate change while running shows none.
  `imu_fifo.c` drops the samples of the settling time after the FIFO
  start and marks the first sample that passes as a discontinuity.
  Confirmed on the prototype: with the discard the z turn in the first
  stats line is the bias drift only. The settling time is still a
  choice, not a datasheet figure; it can only be shortened by a
  measurement.
- filter start (chapter 10): decided. The filter no longer starts at the
  identity: the first sample after boot or a reset sets tilt from the
  accelerometer and heading from the magnetometer (`madgwick_align()`,
  host tested), because converging from the identity took about 15 s
  and tilted the estimate on the way. If no usable field exists at that
  moment, the heading is set by the first step that has one. Open: an
  alignment taken while the tag accelerates has a wrong tilt until the
  correction removes it; accept, or wait for a quiet sample, after bench
  time.
- longest filter step (chapter 10): the longest step is several times
  the typical one, above the budget of R2, while the typical step is
  unchanged. It shows in every stats interval at 960 Hz and now and then
  at 240 Hz, so it is not the INT1 handler, which cannot fire during a
  burst pass at 240 Hz. Unverified: an interrupt landing inside the
  step, with the kernel timer or the I2C transfer of the magnetometer
  poll as candidates. Find which, then decide whether R2 is stated for
  the typical or the longest step.
- cost of the corrected rate (chapters 9 and 10): after the change to a
  rate in millihertz the typical burst pass and the typical filter step
  are slightly longer on the prototype. The 64-bit division per sample
  in the decoder was suspected and removed (one division per burst
  now); the bench run shows that it was only a small part of the
  increase. The rest is unexplained. The stats line now splits a burst
  pass into the FIFO read and the rest; find the largest part from that
  before optimising further.
- step timing reads zero (chapters 9 and 10): resolved as an instrument
  effect. Reconnecting the RTT viewer in mid-run froze the cycle counter
  for seconds: the zero counter jumped and the other DWT lines read 0.
  The block of zeros early after boot is the viewer attaching. Rule for
  every DWT entry in `docs/MEASUREMENTS.md`: probe connected before the
  reset and left alone; zero readings are counted and kept out of the
  figures.
- magnetometer axes (chapter 10 task 8): `mag_poll.c` passes the LIS2MDL
  axes through as the driver reports them. Their alignment with the
  LSM6DSV16X axes on the board is unverified; do the bench check and add
  the remap in `mag_poll_fn()` if the pattern differs.
- magnetometer stuck threshold (chapter 10): 20 identical polls mark the
  sample invalid, because the upstream driver returns 0 after a failed
  I2C transfer. The threshold is a choice; revisit after bench time.
- calibration commands (chapter 12): `imu_cal_gyro_start()`,
  `imu_cal_mag_start()`, `imu_cal_mag_finish()`, the two save functions
  and `app_config_set_fusion()` with `fusion_sink_config_changed()` have
  no caller until the command path exists. The bench check of chapter 10
  uses temporary calls.
- calibration model (later): the magnetometer fit is diagonal (hard iron
  plus per-axis scale, no cross-axis terms) and the accelerometer is not
  calibrated. Revisit if the measured tilt or heading error asks for it.
- sensor-side fusion (later): the LSM6DSV16X game rotation vector (SFLP)
  is an option for a low-power posture mode; not used, because it has no
  magnetometer input in the HAL, cannot run on the host and costs FIFO
  rows.
- long FIFO burst check (chapter 9): `imu_ext` uses its own SPI bus
  functions with a 16-bit length (chapter 8, ADR-004). Prove on the bench
  that a burst above 36 rows arrives complete and record the entry.
- gyro scale check (chapter 10): the decoder uses the HAL sensitivity of
  70 mdps per LSB at 2000 dps; the private firmware used full scale over
  32768. Prove the showcase value with a full turn on the bench and
  record the entry; state the full scale the BLE packet assumes
  (chapter 12).
- sample timestamps (chapter 9): decided. Microseconds of MCU uptime in
  32 bits; the time anchor of a burst is the INT1 edge time stored by the
  ISR, the status-read time is the fallback; consumers take dt from the
  sample rate, never from timestamp differences (review F9). The code is
  in (`imu_fifo.c`, `fifo_decode.c`) and `CLAUDE.md` rules 4 and 9 are
  reworded; open until the anchor measurements of chapter 9 are taken on
  the bench.
- sensor hardware timestamp (later): the LSM6DSV16X can batch its own
  timestamp into the FIFO. Not used; revisit only if the measured
  deviation of the sensor rate from nominal matters for the background
  profile.
- ODR setter guard (chapter 13): `imu_ext_set_odr()` must refuse with
  `-EBUSY` while event triggers are armed (`CLAUDE.md` rule 6, ADR-003).
  The guard comes with `event_triggers_arm()`; until then nothing can arm.
- DSP result ring (chapter 12): the spectrum packets must not share the
  lossy live-stream ring; second ring, flushed by the DSP sink after the
  last chunk (review F8).
- discontinuity flag (chapters 9 and 11): the decoder marks the first
  sample after an overrun, a clipped burst or a FIFO flush (done, host
  tested). Open: the accumulator restarts its window on it (review F3),
  and the caller of `imu_ext_set_odr()` marks the stream once the command
  path exists (chapter 12).
- watermark unit (chapters 9 and 18): confirmed on the bench, the
  watermark counts rows, two per sample (`M-09-0`); `CLAUDE.md` rule 10
  states it. Open: `docs/adr/ADR-001.md` does not exist yet; when it is
  written (chapter 18) it takes the unit and the measured burst period
  from `M-09-0`.
- `burst_proc` stack (chapters 9 and 10): raised to 2048 bytes for the
  decoder and the sinks without a measurement; the fusion sink adds the
  float locals of the 9-axis step and the FPU frame. Size it with the
  thread analyzer after 9-axis fusion at 960 Hz.

## Pre-publication checks

Run from the repository root in PowerShell before every push. Each command
must print nothing.

- no private driver include, no cast into the driver, no cast of a bus
  function to a HAL pointer type (chapter 8, ADR-004):
  `Get-ChildItem -Recurse -File firmware\src | Select-String -Pattern '->config|lsm6dsv16x_config|lsm6dsv16x\.h"|stmemsc|stmdev_(read|write)_ptr\)'`
- no `TODO` in published code (`CLAUDE.md`, publishing rules):
  `Get-ChildItem -Recurse -File firmware\src, firmware\tests | Select-String -CaseSensitive -Pattern 'TODO'`

## Later, after the showcase ships

- upstream contribution path for the IMU (not now): the Zephyr driver has a
  FIFO stream mode (`CONFIG_LSM6DSV16X_STREAM`, RTIO asynchronous sensor
  API) that could replace the own FIFO path in `imu_fifo` and `imu_ext`,
  and it lacks the free-fall and tap triggers this firmware configures
  through the ST HAL. Contributing those triggers as a Zephyr pull request
  is a larger and less predictable project than the showcase itself. The
  current design, own HAL context behind one owner (ADR-004), is the common
  product choice and stays; what matters for review is the clean,
  documented and measured boundary. Revisit after the first FOTA release.

## Closed

- revert reg1 LDO override (chapter 1b): `&reg1` is now
  `NRF5X_REG_MODE_DCDC` with the schematic comment kept.
- drop rev 1.0.0 (chapter 1b): single revision, overlay and 1.1.0 pinctrl
  folded into the base files, `Kconfig.board` and the `revision:` block
  removed.
