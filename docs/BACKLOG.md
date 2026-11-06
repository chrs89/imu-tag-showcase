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

## Closed

- revert reg1 LDO override (chapter 1b): `&reg1` is now
  `NRF5X_REG_MODE_DCDC` with the schematic comment kept.
- drop rev 1.0.0 (chapter 1b): single revision, overlay and 1.1.0 pinctrl
  folded into the base files, `Kconfig.board` and the `revision:` block
  removed.
