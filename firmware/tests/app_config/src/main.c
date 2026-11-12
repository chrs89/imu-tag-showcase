#include <zephyr/ztest.h>
#include <errno.h>
#include "app/app_config.h"

/* Runs after every case: s_cfg is static, so each case hands back defaults. */
static void restore_defaults(void *fixture)
{
        ARG_UNUSED(fixture);
        (void)app_config_set_imu(240, 2);
        (void)app_config_set_ble_downsample(1);
        (void)app_config_set_acq_profile(ACQ_PROFILE_LIVE_REALTIME);
        (void)app_config_set_dsp(0, 0, 0);
}

ZTEST(app_config, test_defaults_match_board_file)
{
        app_config_t cfg;

        app_config_snapshot(&cfg);
        zassert_equal(cfg.imu_odr_hz, 240);
        zassert_equal(cfg.accel_range_g, 2);
        zassert_equal(cfg.ble_downsample, 1);
        zassert_equal(cfg.acq_profile, ACQ_PROFILE_LIVE_REALTIME);
}

ZTEST(app_config, test_invalid_range_rejected_and_config_unchanged)
{
        app_config_t before, after;

        app_config_snapshot(&before);
        zassert_equal(app_config_set_imu(240, 3), -EINVAL);
        app_config_snapshot(&after);

        zassert_equal(after.imu_odr_hz, before.imu_odr_hz);
        zassert_equal(after.accel_range_g, before.accel_range_g);
}

ZTEST(app_config, test_set_dsp_changes_whole_group)
{
        app_config_t cfg;

        zassert_equal(app_config_set_dsp(1, 2, 0x01), 0);
        app_config_snapshot(&cfg);

        zassert_equal(cfg.dsp.axis, 1);
        zassert_equal(cfg.dsp.window_idx, 2);
        zassert_equal(cfg.dsp.enable_mask, 0x01);
}

ZTEST_SUITE(app_config, NULL, NULL, NULL, restore_defaults, NULL);