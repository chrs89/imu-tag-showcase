#include "app/app_config.h"
#include <zephyr/kernel.h>
#include <errno.h>
#include <stdbool.h>

/* Both private to this file. Nothing outside can name them. */
static K_MUTEX_DEFINE(s_cfg_lock);                       /* kernel.h 3432 */
static app_config_t s_cfg = {
        .imu_odr_hz = 240,                               /* board file: accel-odr 240 Hz */
        .accel_range_g = 2,                              /* board file: accel-range 2 g */
        .ble_downsample = 1,
        .acq_profile = ACQ_PROFILE_LIVE_REALTIME,
        .dsp = { .axis = 0, .window_idx = 0, .enable_mask = 0 },
};

/* Validation helpers. All run with no lock held. */

/* The ODRs imu_ext_set_odr() accepts; chapter 8 owns the register table. */
static const uint16_t s_odr_table_hz[] = { 15, 30, 60, 120, 240, 480, 960 };

static bool odr_valid(uint16_t odr_hz)
{
        for (size_t i = 0; i < ARRAY_SIZE(s_odr_table_hz); i++) {
                if (s_odr_table_hz[i] == odr_hz) {
                        return true;
                }
        }
        return false;
}

/* 2, 4, 8 or 16 g: the set imu_ext_set_accel_range() accepts. */
static bool range_valid(uint8_t range_g)
{
        return range_g == 2 || range_g == 4 || range_g == 8 || range_g == 16;
}

/* An enum parameter does not stop a caller from passing 17. */
static bool profile_valid(acq_profile_t profile)
{
        switch (profile) {
        case ACQ_PROFILE_LIVE_REALTIME:
        case ACQ_PROFILE_LIVE_BALANCED:
        case ACQ_PROFILE_RECORD_LIVE:
        case ACQ_PROFILE_RECORD_ONLY:
        case ACQ_PROFILE_BACKGROUND:
                return true;
        default:
                return false;
        }
}

static bool dsp_params_valid(uint8_t axis, uint8_t window_idx)
{
        return axis < APP_DSP_AXIS_COUNT && window_idx < APP_DSP_WINDOW_COUNT;
}

/* Read path: the only way out of s_cfg is a copy taken under the lock. */

void app_config_snapshot(app_config_t *out)
{
        k_mutex_lock(&s_cfg_lock, K_FOREVER);
        *out = s_cfg;                                    /* one consistent struct */
        k_mutex_unlock(&s_cfg_lock);
}

/* Write path: validate first, lock once, write the whole field group. */

int app_config_set_imu(uint16_t odr_hz, uint8_t accel_range_g)
{
        if (!odr_valid(odr_hz) || !range_valid(accel_range_g)) {
                return -EINVAL;
        }
        k_mutex_lock(&s_cfg_lock, K_FOREVER);
        s_cfg.imu_odr_hz = odr_hz;                       /* a reader scales by both, */
        s_cfg.accel_range_g = accel_range_g;             /* so both change under one lock */
        k_mutex_unlock(&s_cfg_lock);
        return 0;
}

int app_config_set_ble_downsample(uint8_t ratio)
{
        if (ratio == 0) {
                return -EINVAL;
        }
        k_mutex_lock(&s_cfg_lock, K_FOREVER);
        s_cfg.ble_downsample = ratio;
        k_mutex_unlock(&s_cfg_lock);
        return 0;
}

int app_config_set_acq_profile(acq_profile_t profile)
{
        if (!profile_valid(profile)) {
                return -EINVAL;
        }
        k_mutex_lock(&s_cfg_lock, K_FOREVER);
        s_cfg.acq_profile = profile;
        k_mutex_unlock(&s_cfg_lock);
        return 0;
}

int app_config_set_dsp(uint8_t axis, uint8_t window_idx, uint8_t enable_mask)
{
        if (!dsp_params_valid(axis, window_idx)) {
                return -EINVAL;
        }
        k_mutex_lock(&s_cfg_lock, K_FOREVER);
        s_cfg.dsp.axis = axis;
        s_cfg.dsp.window_idx = window_idx;
        s_cfg.dsp.enable_mask = enable_mask;
        k_mutex_unlock(&s_cfg_lock);
        return 0;
}
