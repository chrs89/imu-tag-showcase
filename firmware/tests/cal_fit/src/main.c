/* Host tests of the calibration arithmetic (wiki chapter 10 task 6). */
#include <zephyr/ztest.h>
#include <errno.h>
#include "imu/cal_fit.h"

/* A constant input is its own mean and has no spread. The values are exact
 * in binary, so the sums carry no rounding. */
ZTEST(cal_fit, test_gyro_constant_run)
{
	static const float g[3] = {0.5f, -0.25f, 0.125f};
	struct cal_gyro_acc acc = {0};
	float bias[3], spread;

	for (int i = 0; i < 480; i++) {
		cal_gyro_add(&acc, g);
	}
	zassert_ok(cal_gyro_finish(&acc, bias, &spread));
	zassert_equal(acc.n, 480);
	zassert_equal(bias[0], 0.5f);
	zassert_equal(bias[1], -0.25f);
	zassert_equal(bias[2], 0.125f);
	zassert_equal(spread, 0.0f);
}

/* Alternating +0.01 and -0.01 rad/s on one axis: mean 0, deviation 0.01. */
ZTEST(cal_fit, test_gyro_spread_is_worst_axis)
{
	struct cal_gyro_acc acc = {0};
	float bias[3], spread;

	for (int i = 0; i < 480; i++) {
		const float g[3] = {0.0f, (i & 1) ? -0.01f : 0.01f, 0.0f};

		cal_gyro_add(&acc, g);
	}
	zassert_ok(cal_gyro_finish(&acc, bias, &spread));
	zassert_within(bias[0], 0.0f, 1e-7f);
	zassert_within(bias[1], 0.0f, 1e-7f);
	zassert_within(bias[2], 0.0f, 1e-7f);
	zassert_within(spread, 0.01f, 1e-5f);
}

ZTEST(cal_fit, test_gyro_empty_run_rejected)
{
	const struct cal_gyro_acc acc = {0};
	float bias[3] = {7.0f, 7.0f, 7.0f}, spread = 7.0f;

	zassert_equal(cal_gyro_finish(&acc, bias, &spread), -ENODATA);
	zassert_equal(bias[0], 7.0f);
	zassert_equal(spread, 7.0f);
}

/* Radii 40, 40 and 20 uT, mean 33.33: bias is the centre, scale brings
 * every axis to the mean radius. */
ZTEST(cal_fit, test_mag_fit_bias_and_scale)
{
	static const float min[3] = {-30.0f, -40.0f, -20.0f};
	static const float max[3] = {50.0f, 40.0f, 20.0f};
	float bias[3], scale[3];

	zassert_ok(cal_mag_fit(min, max, 5.0f, bias, scale));
	zassert_within(bias[0], 10.0f, 1e-5f);
	zassert_within(bias[1], 0.0f, 1e-5f);
	zassert_within(bias[2], 0.0f, 1e-5f);
	zassert_within(scale[0], 0.8333f, 1e-4f);
	zassert_within(scale[1], 0.8333f, 1e-4f);
	zassert_within(scale[2], 1.6667f, 1e-4f);
}

/* One axis hardly moved: the tag was not turned through all orientations. */
ZTEST(cal_fit, test_mag_fit_small_radius_rejected)
{
	static const float min[3] = {-30.0f, -40.0f, -2.0f};
	static const float max[3] = {50.0f, 40.0f, 2.0f};
	float bias[3] = {7.0f, 7.0f, 7.0f}, scale[3] = {7.0f, 7.0f, 7.0f};

	zassert_equal(cal_mag_fit(min, max, 5.0f, bias, scale), -ENODATA);
	zassert_equal(bias[0], 7.0f);
	zassert_equal(scale[2], 7.0f);
}

ZTEST_SUITE(cal_fit, NULL, NULL, NULL, NULL, NULL);
