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
