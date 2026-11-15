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
 *  make one sample: the watermark counts rows (docs/MEASUREMENTS.md M-09-0,
 *  CLAUDE.md rule 10). Every value is even
 *  and fits the 8-bit watermark register. */
typedef enum {
        ACQ_PROFILE_LIVE_REALTIME = 8,     /* 4 samples per burst, live BLE */
        ACQ_PROFILE_LIVE_BALANCED = 16,    /* 8 samples per burst, monitoring */
        ACQ_PROFILE_BACKGROUND    = 254,   /* 127 samples per burst, background */
} acq_profile_t;

/** What the fusion sink does with a sample. */
typedef enum {
        FUSION_MODE_OFF   = 0,             /* no filter step; gyro calibration still works */
        FUSION_MODE_6AXIS = 1,             /* gyro and accel; yaw is the gyro integral */
        FUSION_MODE_9AXIS = 2,             /* plus the magnetometer while its sample is usable */
} fusion_mode_t;

#define APP_FUSION_BETA_X1000_MIN 10       /* beta 0.01 rad/s */
#define APP_FUSION_BETA_X1000_MAX 1000     /* beta 1.0 rad/s */

typedef struct {
        uint16_t imu_odr_hz;               /* default 240, board file accel-odr */
        uint8_t accel_range_g;             /* default 2, board file accel-range */
        uint8_t ble_downsample;            /* 1 = every sample, 2 = every second, 1..255 */
        acq_profile_t acq_profile;         /* default ACQ_PROFILE_LIVE_REALTIME */
        fusion_mode_t fusion_mode;         /* default FUSION_MODE_9AXIS */
        uint16_t fusion_beta_x1000;        /* filter gain beta in rad/s times 1000; default 100 */
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

/** Called from app_wq only. Validates, then locks once; the caller tells the
 *  fusion sink with fusion_sink_config_changed().
 *  @return 0, or -EINVAL for an unknown mode or a beta outside
 *  APP_FUSION_BETA_X1000_MIN to APP_FUSION_BETA_X1000_MAX. */
int app_config_set_fusion(fusion_mode_t mode, uint16_t beta_x1000);

#endif