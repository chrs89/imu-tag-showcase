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

## Baselines

- RAM baseline after chapter 2 (2026-11-06, `west build -d build/firmware
  -t ram_report`, application image only): 9584 B of 256 KB used (3.66 %),
  flash 37252 B of 499200 B (7.46 %). Largest items: interrupt stack 2048 B,
  RTT up buffer 1024 B, main stack 1024 B, CryptoCell platform context
  448 B. Chapter 2b compares against this.
- RAM after chapter 2b stubs (2026-11-06, same report): 10776 B (4.21 %),
  +1192 B. The first work item makes the linker keep the system work queue
  thread: 1024 B stack plus thread and queue structs. Flash 37976 B with
  all three stubs.

## Closed

- revert reg1 LDO override (chapter 1b): `&reg1` is now
  `NRF5X_REG_MODE_DCDC` with the schematic comment kept.
- drop rev 1.0.0 (chapter 1b): single revision, overlay and 1.1.0 pinctrl
  folded into the base files, `Kconfig.board` and the `revision:` block
  removed.
