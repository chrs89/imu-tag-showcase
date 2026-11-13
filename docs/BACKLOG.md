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
- hardware FPU (chapters 10 and 11): the build does not set `CONFIG_FPU`,
  so float arithmetic runs as software calls. Enable `CONFIG_FPU` and
  `CONFIG_FPU_SHARING` before the first float sink runs on `burst_proc`
  (fusion, chapter 10), add `CONFIG_CMSIS_DSP` with the FFT (chapter 11),
  then re-measure thread stacks and name the FPU setting in every timing
  entry.
- long FIFO burst check (chapter 9): `imu_ext` uses its own SPI bus
  functions with a 16-bit length (chapter 8, ADR-004). Prove on the bench
  that a burst above 36 rows arrives complete and record the entry.
- gyro scale check (chapter 10): the decoder uses the HAL sensitivity of
  70 mdps per LSB at 2000 dps; the private firmware used full scale over
  32768. Prove the showcase value with a full turn on the bench and
  record the entry; state the full scale the BLE packet assumes
  (chapter 12).
- timestamp resolution (chapter 10): samples carry milliseconds; decide
  between dt from the sample rate and a microsecond field (review F9).
- ODR setter guard (chapter 13): `imu_ext_set_odr()` must refuse with
  `-EBUSY` while event triggers are armed (`CLAUDE.md` rule 6, ADR-003).
  The guard comes with `event_triggers_arm()`; until then nothing can arm.
- DSP result ring (chapter 12): the spectrum packets must not share the
  lossy live-stream ring; second ring, flushed by the DSP sink after the
  last chunk (review F8).
- discontinuity flag (chapter 9): the decoder marks the first sample after
  an overrun or a FIFO flush; the accumulator restarts its window on it
  (review F3).

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
