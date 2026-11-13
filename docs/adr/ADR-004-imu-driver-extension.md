# ADR-004: The upstream IMU driver probes, one application module owns the registers

## Status

Accepted, 2026-11-13. Implemented in `firmware/src/imu/imu_ext.c` (commit
0e7bc80). Bench measurements `M-08-1` to `M-08-3` are pending; this record
contains no measured figure.

## Context

The firmware uses an ST LSM6DSV16X on SPI1. Zephyr ships a driver for it
(`zephyr/drivers/sensor/st/lsm6dsv16x/`, NCS v3.3.0). What this product needs
from the sensor and what that driver offers do not match:

| The firmware needs | The upstream driver offers | Source |
| --- | --- | --- |
| a probe at boot that fails loudly | yes: bank select, WHO_AM_I check, reset | `lsm6dsv16x.c` 1058, 1085-1100, 1115 |
| boot rates and ranges from the board file | yes | `lsm6dsv16x.c` 1283-1300 |
| FIFO burst reads at up to 1920 rows per second, drained by an own thread | only through the RTIO stream mode | `Kconfig` 26-31, `lsm6dsv16x_rtio_stream.c` 217-221 |
| rate change as one sequence: FIFO bypass, output rate, both batch rates, FIFO restart | `attr_set` changes the output rate only | `lsm6dsv16x.c` 297, 391, 420 |
| free-fall, tap and motion events on INT2 | data-ready and wake-up only | `lsm6dsv16x_trigger.c` 182, 201 |
| INT1 handled by a one-line ISR and a semaphore | its own trigger thread or work item | `Kconfig` 35-42 |

The predecessor of this firmware closed the gap by casting the driver's
`dev->config` to a one-field struct to reach the driver's ST HAL context. That
depends on the field order of `struct lsm6dsv16x_config` (`lsm6dsv16x.h`
58-104, `ctx` at line 59), a header under `drivers/` that is not an API.

## Requirements

| # | Requirement | Follows from |
| --- | --- | --- |
| R1 | Full register access through the ST HAL | FIFO, batch rates and embedded events are not in the sensor API |
| R2 | No dependency on a header or struct layout that is not an API | the SDK is updated during the product's life; update images must keep building |
| R3 | Hardware facts come from the devicetree only | a board revision changes the board file, not C |
| R4 | One owner serialises register sequences, in thread context | a rate change must not interleave with a FIFO read |
| R5 | No copy of the driver to maintain | the driver directory is 4722 lines and changes with SDK releases |
| R6 | No second asynchronous framework | the device already carries the Bluetooth stack |

## Options considered

| Option | R1 | R2 | R3 | R4 | R5 | R6 | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A. Sensor API only (`sensor_sample_fetch`, `attr_set`) | no | yes | yes | no | yes | yes | cannot do the job |
| B. Upstream driver plus a cast into `dev->config` | yes | no | yes | partly | yes | yes | breaks silently when the struct changes |
| C. Fork the driver out of tree | yes | yes | yes | yes | no | yes | works, costs maintenance for ever |
| D. Upstream RTIO stream mode, contribute the missing triggers upstream | partly | yes | yes | yes | yes | no | the upstream-minded path; larger than this project |
| D2. Upstream stream mode for the data, events added in the application | yes | partly | yes | no | yes | no | two owners of the registers at run time |
| E. No driver, ST HAL only | yes | yes | yes | yes | yes | yes | loses the probe; the HAL has no user-settable Kconfig symbol |
| F. Upstream driver at boot, own HAL context in one application module | yes | yes | yes | yes | yes | yes | chosen |

Why D2 fails for this product, in this SDK release:

- The driver's stream path issues its own register transfers at run time
  (`lsm6dsv16x_rtio_stream.c` 401-412, 692-707). The application's mutex
  cannot serialise them.
- Watermark and both batch rates are taken from the devicetree when the
  stream starts (`lsm6dsv16x_rtio_stream.c` 103-108, 129). The acquisition
  profiles and the runtime rate change would need own writes to the same
  registers.
- The stream handler runs on the system work queue or on a driver thread at a
  cooperative priority (`lsm6dsv16x_trigger.c` 289, 403-406), outside the
  thread priorities this firmware assigns to acquisition.
- It needs `SPI_RTIO`, marked experimental (`zephyr/drivers/spi/Kconfig`
  32-36), and delivers the driver's private frame format
  (`lsm6dsv16x_decoder.h` 28).

Why E is not chosen: `USE_STDC_LSM6DSV16X`, which compiles the ST HAL, has no
prompt (`zephyr/modules/hal_st/Kconfig` 195-196). The driver's Kconfig selects
it (`lsm6dsv16x/Kconfig` 17-18). Keeping the driver enabled brings the HAL
into the build, a tested probe, and a `struct device` whose readiness `main()`
checks.

## Decision

1. The upstream driver is enabled with `CONFIG_LSM6DSV16X_TRIGGER_NONE=y`. It
   runs once, at `POST_KERNEL` priority 90, before `main()`: it checks the
   chip ID, resets the chip and applies the board-file defaults. With
   `TRIGGER_NONE` its config struct has no interrupt GPIO fields
   (`lsm6dsv16x.c` 1272-1281), so INT1 and INT2 belong to the application.
2. After boot every register access goes through `imu_ext`, under one mutex,
   `s_imu_lock`. `main()` calls `device_is_ready()` on the driver as the boot
   health check and then `imu_ext_init()`.
3. `imu_ext` builds its own ST HAL context (`stmdev_ctx_t`) from public
   headers only: `zephyr/drivers/spi.h` and the HAL's `lsm6dsv16x_reg.h`. The
   SPI specification comes from `DT_ALIAS(imu0)` with the same operation word
   as the driver (`lsm6dsv16x.c` 1304-1307).
4. The context uses two bus functions of the module's own with the HAL's
   exact signatures (`lsm6dsv16x_reg.h` 111-112), not the driver tree's
   `STMEMSC_CTX_SPI` helper (`stmemsc.h` 94-100). The helper's functions take
   the length as `uint8_t` (`stmemsc_spi.c` 20-21) behind a cast to the HAL's
   `uint16_t` function-pointer type. For the driver's short reads that is
   harmless. A FIFO burst above 36 rows (252 bytes) would rely on undefined
   behaviour.
5. The application does not call the sensor API on this device after boot.
   The driver caches the rate it set at init; `app_config` is the source of
   truth for the sample rate (`CLAUDE.md` rule 1).

## Consequences

- Two register contexts exist for one chip. They are not locked against each
  other. They are separated in time by the init order: the driver's is used
  before `main()`, the module's from `imu_ext_init()` on. `imu_ext_init()`
  is called from `main()`, never from `SYS_INIT`, so the driver's reset
  cannot undo what the module wrote.
- The mutex is taken in the module's public functions, once per register
  sequence, not in the bus functions. The SPI driver already serialises
  single transfers.
- Register access is thread-only: a transfer waits on a semaphore until the
  SPI interrupt, so it cannot run in an ISR or under a spinlock.
- About 35 lines duplicate what the driver tree's bus helper does. In
  exchange `imu_ext.c` includes nothing from `zephyr/drivers/sensor/`, and
  the pre-publication check in `docs/BACKLOG.md` enforces that.
- An SDK update can change the driver's boot sequence. The health check in
  `main()` and the module's own WHO_AM_I read catch a probe that no longer
  succeeds; the module's register writes do not depend on what the driver
  left behind, except the boot rates, which equal the initial profile.
- Option D stays open as later work and is recorded in `docs/BACKLOG.md`
  under "Later, after the showcase ships".

## Evidence

NCS v3.3.0 tree. `zephyr/drivers/sensor/st/lsm6dsv16x/`: `lsm6dsv16x.h`
58-104; `lsm6dsv16x.c` 297, 391, 420, 1058, 1085-1100, 1115, 1272-1281,
1283-1300, 1304-1307; `lsm6dsv16x_trigger.c` 182, 201, 289, 403-406;
`lsm6dsv16x_rtio_stream.c` 103-108, 129, 217-221, 401-412, 692-707;
`lsm6dsv16x_decoder.h` 28; `Kconfig` 17-18, 26-31, 35-42.
`zephyr/drivers/sensor/st/stmemsc/`: `stmemsc.h` 94-100; `stmemsc_spi.c`
20-21. `zephyr/drivers/spi/Kconfig` 32-36. `zephyr/modules/hal_st/Kconfig`
195-196. ST HAL `lsm6dsv16x_reg.h` 111-112.

To be added when measured: `M-08-1` (the two WHO_AM_I reads on the wire),
`M-08-2` (idle current with the sensor at its boot defaults), `M-08-3` (time
of one register read from inside the firmware), and the long-burst check of
chapter 9.
