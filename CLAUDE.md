# IMU Tag showcase - firmware

Public portfolio repository. Zephyr RTOS on nRF Connect SDK 3.3.0, custom board
`imu_tag_v1/nrf52840` (PCBA rev 1.1.0). Sensors: LSM6DSV16X over SPI1, LIS2MDL
over I2C0. Firmware lives in `firmware/`, the Flutter app in `app/` (later).
This repository is a west manifest repository (`west.yml` in the root) in the
`ncs-example-application` layout: the workspace top directory is the parent of
this repository, and the SDK is cloned next to it, never inside it.

## Where to look first

- `docs/ARCHITECTURE.md`: threads, sinks, GATT table, concurrency rules.
- `docs/adr/`: ADR-001 FIFO pipeline, ADR-002 DSP worker, ADR-003 runtime ODR,
  ADR-004 IMU driver extension, ADR-005 command table.
- `firmware/src/imu/imu_ext.h`: the only place that touches LSM6DSV16X registers.

## Rules that are not obvious from the code

1. **One owner for the IMU registers.** All LSM6DSV16X register access goes through
   `imu_ext` under its mutex. Never cast `dev->config` of the upstream driver.
   Never call `sensor_attr_get()` on the LSM6DSV16X after boot; `app_config.imu_odr_hz`
   is the source of truth for the sample rate.
2. **ODR and FIFO batch rate change together.** `imu_ext_set_odr()` must set the
   accel and gyro batch data rate to the same value, then flush the FIFO
   (bypass then stream). Otherwise 480 and 960 Hz silently deliver 240 Hz data.
3. **Sinks are O(1).** Functions registered with `burst_processor_register_sink()`
   run on the acquisition thread for every sample. No sleeping, no `K_FOREVER`
   waits except the documented mag-sample copy, no logging per sample, no FFT.
   Heavy work goes to `dsp_worker` or a work item.
4. **ISRs only signal.** INT1 gives a semaphore, INT2 submits a work item. Nothing else.
5. **Only the system work queue mutates `app_config` and `app_state`.** BLE write
   callbacks schedule a command; they never run handlers inline.
6. **Event triggers are tuned for 240 Hz.** `event_triggers_arm()` refuses any other
   ODR, and the ODR setter refuses while triggers are armed (ADR-003). Keep both
   guards in sync.
7. **Watchdog timeouts derive from the acquisition profile.** The `burst_proc`
   channel timeout must be at least three burst periods at the current watermark
   and ODR. Do not hard-code seconds.
8. **Register reconfiguration happens with the FIFO in bypass.** Feature setup
   (tap, free-fall, motion) and range or rate changes are not done mid-stream.
9. **Units:** accel in m/s2, gyro in rad/s, mag in uT, timestamps in ms from
   `k_uptime_get_32()`. BLE packets carry Q1.15 scaled by the runtime full scale.
10. **Watermark unit: unverified.** The binding and the observed 59 Hz batch rate
    suggest one FIFO word (tag plus 6 bytes), so 8 words would be 4 accel+gyro
    pairs. Until one bench measurement confirms it, no published latency figure
    may depend on it. Replace this rule with the measured fact when known.

## Build and test

Workspace setup, once (the README shows the same):

```
west init -m https://github.com/<owner>/imu-tag-showcase --mr main ws
cd ws
west update
```

Build and test, from this repository root (west finds `.west` in the parent):

```
west build -b imu_tag_v1/nrf52840 --sysbuild firmware
west flash
west twister -p native_sim -T firmware/tests    # Linux or WSL only; CI on ubuntu-latest is authoritative
```

MCUboot signs with its development key unless `SB_CONFIG_BOOT_SIGNATURE_KEY_FILE`
is set. Never commit a private key. The README states the dev-key situation.

The Partition Manager lays out internal flash dynamically; the devicetree
`fixed-partitions` node is not used. No image may be released for DFU until
`firmware/pm_static.yml` freezes the map (NCS: static partition requirement
for DFU). Until then every build is lab-only.

## Publishing rules

- No employer names, supplier names, site names, personal names other than the
  owner, API keys, `.env`, keystores. Grep before every push.
- No `TODO` in published code; open items go to `docs/BACKLOG.md`.
- No file over about 400 lines. No feature without a test or a documented reason.
- English only. Every public function has a one-line doc comment.
- Comments state facts verified on hardware or in the SDK tree, not assumptions.
  If something is unverified, say "unverified" in the comment.

## Style

- Zephyr coding style (tabs, 100 columns, `LOG_MODULE_REGISTER` per file).
- Static module state prefixed `s_`, cross-thread scalars are `atomic_t`.
- Return negative errno. Validate every BLE payload length before reading it.
- Prefer `K_WORK_DELAYABLE_DEFINE`, `K_SEM_DEFINE`, `K_MUTEX_DEFINE` over runtime init.

## When unsure

Read the matching Zephyr skill (zephyr-foundations, kernel-services, devicetree,
connectivity-ble, testing-debugging, security-updates) before writing code, and
verify any API against `C:\ncs\v3.3.0\zephyr` or `C:\ncs\v3.3.0\nrf`, not memory.

## Decisions fixed by the owner (2026-11-02)

- Protocol compatibility target is the Flutter repository `imu_tag_v1`. The
  older `imu_tag_app` is stale and protocol-mismatched; never take protocol
  facts from it.
- `led_indicator` compiles out cleanly when the board has no `led0` alias.
- `bench_shell` is off by default, gated by `CONFIG_IMU_TAG_BENCH_SHELL`, and
  stays under about 150 lines.
- Only PCBA rev 1.1.0 exists in the board files. No rev 1.0.0 nodes, no
  revision block, no diagnostic overrides (the `&reg1` LDO override is gone).
- Repository layout follows `ncs-example-application`: `west.yml` in the
  repository root with `self.path: imu-tag-showcase`, no `.west` in the repo.
  Workspace setup is `west init -m <repo url> --mr main ws`, `cd ws`,
  `west update`; the README shows it. No SDK clone ever lands inside the
  repository, so there is nothing SDK-related to gitignore.
- `app/` contains one README paragraph until the app phase starts.
- CI is split by path: firmware jobs trigger on `firmware/**` and `docs/**`,
  app jobs (later) on `app/**`.
- Open items go to `docs/BACKLOG.md`, present since the first commit.
