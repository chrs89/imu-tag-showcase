# IMU Tag showcase

Firmware for a wearable IMU tag: Zephyr RTOS on nRF Connect SDK 3.3.0, custom
board `imu_tag_v1/nrf52840` (nRF52840, LSM6DSV16X over SPI, LIS2MDL over I2C).
Features: vibration analysis, orientation, live data over BLE, FOTA, NFC
wake-up and pairing, IMU hardware triggers.

- `firmware/` the Zephyr application
- `app/` the companion Flutter app (next phase)
- `docs/` architecture, ADRs, reviews, backlog

## Getting the code

This repository is a west manifest repository. The workspace top directory is
the parent of the repository and the SDK is cloned next to it, never inside it.

```
west init -m https://github.com/chrs89/imu-tag-showcase --mr main ws
cd ws
west update
```

## Building

From `ws/imu-tag-showcase`:

```
west build -b imu_tag_v1/nrf52840 --sysbuild firmware
west flash
```

The build needs the nRF Connect SDK v3.3.0 toolchain. MCUboot signs the
application with its development key unless `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`
is set; images built here are not production-signed.

## Status

Firmware transfer in progress. See `docs/BACKLOG.md`.
