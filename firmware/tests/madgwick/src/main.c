/* Host tests of the pure Madgwick filter (wiki chapter 10 task 3).
 * beta 0.1 and dt = 1 / 240 unless a case says otherwise. Earth frame:
 * x north, y west, z up; q rotates body to earth. */
#include <zephyr/ztest.h>
#include <errno.h>
#include <math.h>
#include "fusion/madgwick.h"

#define BETA 0.1f
#define DT   (1.0f / 240.0f)
#define G    9.80665f

static void expect_q(const madgwick_t *f, float w, float x, float y, float z, float tol)
{
	zassert_within(f->q0, w, tol, "q0 %d e-6", (int)(f->q0 * 1e6f));
	zassert_within(f->q1, x, tol, "q1 %d e-6", (int)(f->q1 * 1e6f));
	zassert_within(f->q2, y, tol, "q2 %d e-6", (int)(f->q2 * 1e6f));
	zassert_within(f->q3, z, tol, "q3 %d e-6", (int)(f->q3 * 1e6f));
}

/* (a) At rest and aligned, nothing moves the estimate. */
ZTEST(madgwick, test_rest_stays_at_identity)
{
	madgwick_t f6, f9;

	madgwick_init(&f6, BETA);
	madgwick_init(&f9, BETA);
	for (int i = 0; i < 2400; i++) {
		zassert_ok(madgwick_update_6axis(&f6, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, G, DT));
		zassert_ok(madgwick_update_9axis(&f9, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, G, 20.0f,
						 0.0f, -43.0f, DT));
	}
	zassert_equal(f6.q0, 1.0f);
	zassert_equal(f6.q1, 0.0f);
	zassert_equal(f6.q2, 0.0f);
	zassert_equal(f6.q3, 0.0f);
	zassert_equal(f9.q0, 1.0f);
	zassert_equal(f9.q1, 0.0f);
	zassert_equal(f9.q2, 0.0f);
	zassert_equal(f9.q3, 0.0f);
}

/* (b) Gravity pulls the estimate at 2 * beta = 11.5 degrees per second
 * towards a tilt of 30 degrees about x, and it arrives. */
ZTEST(madgwick, test_gravity_alignment_rate_and_target)
{
	madgwick_t f;

	madgwick_init(&f, BETA);
	for (int i = 0; i < 240; i++) {
		zassert_ok(madgwick_update_6axis(&f, 0.0f, 0.0f, 0.0f, 0.0f, 4.9033f, 8.4928f,
						 DT));
	}
	zassert_true(f.q1 > 0.08f && f.q1 < 0.12f, "q1 after 1 s: %d e-6", (int)(f.q1 * 1e6f));

	for (int i = 240; i < 960; i++) {
		zassert_ok(madgwick_update_6axis(&f, 0.0f, 0.0f, 0.0f, 0.0f, 4.9033f, 8.4928f,
						 DT));
	}
	expect_q(&f, 0.96593f, 0.25882f, 0.0f, 0.0f, 0.002f);
}

/* (c) A zero magnetometer vector makes the 9-axis step a 6-axis step. */
ZTEST(madgwick, test_zero_mag_equals_6axis)
{
	madgwick_t f6, f9;

	madgwick_init(&f6, BETA);
	madgwick_init(&f9, BETA);
	for (int i = 0; i < 500; i++) {
		const float t = (float)i * DT;
		const float gx = 0.8f * sinf(3.0f * t);
		const float gy = -0.5f * cosf(2.0f * t);
		const float gz = 0.3f;
		const float ax = 2.0f * sinf(t);
		const float ay = 1.5f * cosf(1.7f * t);
		const float az = G;

		zassert_ok(madgwick_update_6axis(&f6, gx, gy, gz, ax, ay, az, DT));
		zassert_ok(madgwick_update_9axis(&f9, gx, gy, gz, ax, ay, az, 0.0f, 0.0f, 0.0f,
						 DT));
	}
	expect_q(&f9, f6.q0, f6.q1, f6.q2, f6.q3, 1e-6f);
}

/* (d) A constant rate integrates to the same angle at every sample rate. */
ZTEST(madgwick, test_constant_rate_at_three_sample_rates)
{
	static const int rates[] = {240, 480, 960};

	for (size_t r = 0; r < ARRAY_SIZE(rates); r++) {
		const float dt = 1.0f / (float)rates[r];
		madgwick_t f;

		madgwick_init(&f, BETA);
		for (int i = 0; i < rates[r]; i++) {
			zassert_ok(madgwick_update_6axis(&f, 0.0f, 0.0f, 1.5707963f, 0.0f, 0.0f,
							 G, dt));
		}
		expect_q(&f, 0.70711f, 0.0f, 0.0f, 0.70711f, 0.001f);

		for (int i = 0; i < 3 * rates[r]; i++) {
			zassert_ok(madgwick_update_6axis(&f, 0.0f, 0.0f, 1.5707963f, 0.0f, 0.0f,
							 G, dt));
		}
		zassert_true(fabsf(f.q0) > 0.9999f, "q0 after one turn at %d Hz: %d e-6",
			     rates[r], (int)(f.q0 * 1e6f));
	}
}

/* (e) Reference field: a level tag whose x axis points west reads the field
 * as (0, -20, -43). The filter turns from the identity to a yaw of 90 degrees
 * within 15 s. With the reference field at half length (the private form)
 * this takes about 21 s and the case fails. */
ZTEST(madgwick, test_reference_field_finds_north)
{
	madgwick_t f;

	madgwick_init(&f, BETA);
	for (int i = 0; i < 3600; i++) {
		zassert_ok(madgwick_update_9axis(&f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, G, 0.0f,
						 -20.0f, -43.0f, DT));
	}
	expect_q(&f, 0.70711f, 0.0f, 0.0f, 0.70711f, 0.01f);
}

/* (f) A dt that is zero, negative or not a number is refused, q untouched. */
ZTEST(madgwick, test_bad_dt_rejected)
{
	static const float bad[] = {0.0f, -1.0f, NAN};
	madgwick_t f = {.q0 = 0.5f, .q1 = 0.5f, .q2 = 0.5f, .q3 = 0.5f, .beta = BETA};

	for (size_t i = 0; i < ARRAY_SIZE(bad); i++) {
		zassert_equal(madgwick_update_6axis(&f, 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, G, bad[i]),
			      -EINVAL);
		zassert_equal(madgwick_update_9axis(&f, 1.0f, 2.0f, 3.0f, 0.0f, 0.0f, G, 20.0f,
						    0.0f, -43.0f, bad[i]),
			      -EINVAL);
	}
	zassert_equal(f.q0, 0.5f);
	zassert_equal(f.q1, 0.5f);
	zassert_equal(f.q2, 0.5f);
	zassert_equal(f.q3, 0.5f);
}

/* (g) A foreign horizontal field turns yaw by atan(15 / 20) = 36.9 degrees
 * and leaves the tilt alone. */
ZTEST(madgwick, test_foreign_field_leaks_into_yaw_only)
{
	madgwick_t f;

	madgwick_init(&f, BETA);
	for (int i = 0; i < 7200; i++) {
		zassert_ok(madgwick_update_9axis(&f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, G, 20.0f,
						 15.0f, -43.0f, DT));
	}
	zassert_true(fabsf(f.q1) < 0.001f, "q1 %d e-6", (int)(f.q1 * 1e6f));
	zassert_true(fabsf(f.q2) < 0.001f, "q2 %d e-6", (int)(f.q2 * 1e6f));
	zassert_within(f.q3, -0.316f, 0.01f, "q3 %d e-6", (int)(f.q3 * 1e6f));
}

ZTEST_SUITE(madgwick, NULL, NULL, NULL, NULL, NULL);
