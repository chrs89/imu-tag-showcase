#ifndef APP_APP_CONFIG_H
#define APP_APP_CONFIG_H

#include <stdint.h>

/* DSP parameter ranges. Chapter 11 replaces these with the dsp_config.h enums. */
#define APP_DSP_AXIS_COUNT   4             /* X, Y, Z, principal */
#define APP_DSP_WINDOW_COUNT 4             /* 64, 128, 256, 512 samples */

/* Runtime configuration of the IMU Tag. One private copy lives in
 * app_config.c behind s_cfg_lock. Readers take a snapshot, writers go
 * through one setter per field group. No function hands out a pointer
 * into the live struct (chapter 6 section 5, A4 section 7). */

/** FIFO watermark in rows; selects wake rate, latency and power. Two rows
 *  make one sample if the watermark counts rows, as the sensor binding says
 *  (unverified until the bench check, CLAUDE.md rule 10). Every value is even
 *  and fits the 8-bit watermark register. */
typedef enum {
        ACQ_PROFILE_LIVE_REALTIME = 8,     /* 4 samples per burst, live BLE */
        ACQ_PROFILE_LIVE_BALANCED = 16,    /* 8 samples per burst, monitoring */
        ACQ_PROFILE_BACKGROUND    = 254,   /* 127 samples per burst, background */
} acq_profile_t;

typedef struct {
        uint16_t imu_odr_hz;               /* default 240, board file accel-odr */
        uint8_t accel_range_g;             /* default 2, board file accel-range */
        uint8_t ble_downsample;            /* 1 = every sample, 2 = every second, 1..255 */
        acq_profile_t acq_profile;         /* default ACQ_PROFILE_LIVE_REALTIME */
        struct {
                uint8_t axis;              /* dsp axis mode */
                uint8_t window_idx;        /* dsp window index */
                uint8_t enable_mask;       /* dsp output enable bits */
        } dsp;
} app_config_t;

/** Called from any thread; copies under s_cfg_lock. One consistent struct. */
void app_config_snapshot(app_config_t *out);

/** Called from app_wq only. Validates, then locks once.
 *  @return 0, or -EINVAL when a value is outside the accepted set. */
int app_config_set_imu(uint16_t odr_hz, uint8_t accel_range_g);

/** Called from app_wq only. Validates, then locks once.
 *  @return 0, or -EINVAL when ratio is 0. */
int app_config_set_ble_downsample(uint8_t ratio);

/** Called from app_wq only. Validates, then locks once.
 *  @return 0, or -EINVAL when profile is not one of acq_profile_t. */
int app_config_set_acq_profile(acq_profile_t profile);

/** Called from app_wq only. Validates, then locks once.
 *  @return 0, or -EINVAL when axis or window_idx is out of range. */
int app_config_set_dsp(uint8_t axis, uint8_t window_idx, uint8_t enable_mask);

#endif