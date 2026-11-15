#include <zephyr/ztest.h>
#include <string.h>
#include "imu/fifo_decode.h"

#define TAG_GYRO      0x01u
#define TAG_ACCEL     0x02u
#define TAG_TIMESTAMP 0x04u /* a row the decoder does not know */

static imu_sample_t s_out[8];
static size_t s_n;
static uint8_t s_rows[12 * FIFO_ROW_BYTES];
static struct fifo_decode_state s_st;

static const struct fifo_decode_cfg s_cfg = {
	.accel_scale = 1.0f,
	.gyro_scale = 1.0f,
	.rate_mhz = 960000,
};

static void collect(const imu_sample_t *s, void *user)
{
	ARG_UNUSED(user);
	if (s_n < ARRAY_SIZE(s_out)) {
		s_out[s_n] = *s;
	}
	s_n++;
}

/* Row i: tag in the upper five bits of byte 0, then X, Y, Z little-endian. */
static void put_row(uint8_t *buf, size_t i, uint8_t tag, int16_t x, int16_t y, int16_t z)
{
	uint8_t *r = &buf[i * FIFO_ROW_BYTES];
	const int16_t v[3] = {x, y, z};

	r[0] = (uint8_t)(tag << 3);
	for (int k = 0; k < 3; k++) {
		r[1 + 2 * k] = (uint8_t)((uint16_t)v[k] & 0xFFu);
		r[2 + 2 * k] = (uint8_t)((uint16_t)v[k] >> 8);
	}
}

/* n pairs; sample k has accel x = 100 + k and gyro x = 200 + k. */
static void fill_pairs(size_t n, bool gyro_first)
{
	for (size_t k = 0; k < n; k++) {
		const size_t g = gyro_first ? 2 * k : 2 * k + 1;
		const size_t a = gyro_first ? 2 * k + 1 : 2 * k;

		put_row(s_rows, g, TAG_GYRO, (int16_t)(200 + k), 0, 0);
		put_row(s_rows, a, TAG_ACCEL, (int16_t)(100 + k), 0, 0);
	}
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);
	memset(s_out, 0, sizeof(s_out));
	memset(s_rows, 0, sizeof(s_rows));
	memset(&s_st, 0, sizeof(s_st));
	s_n = 0;
}

static void check_four_samples(void)
{
	static const uint32_t ts[4] = {996875u, 997917u, 998958u, 1000000u};

	zassert_equal(s_n, 4);
	for (size_t k = 0; k < 4; k++) {
		zassert_equal(s_out[k].timestamp_us, ts[k], "sample %zu: %u", k,
			      s_out[k].timestamp_us);
		zassert_equal(s_out[k].seq, k);
		zassert_within(s_out[k].accel.x, 100.0f + (float)k, 1e-3f);
		zassert_within(s_out[k].gyro.x, 200.0f + (float)k, 1e-3f);
		zassert_equal(s_out[k].flags, 0);
	}
}

/* (a) eight rows, gyro then accel, anchor on the newest sample. */
ZTEST(fifo_decode, test_gyro_accel_order)
{
	const struct fifo_anchor anchor = {.ts_us = 1000000u, .index = 3};

	fill_pairs(4, true);
	zassert_equal(fifo_decode(s_rows, 8, &s_cfg, &anchor, &s_st, collect, NULL), 4);
	check_four_samples();
	zassert_equal(s_st.orphans, 0);
}

/* (b) the decoder does not rely on which row the sensor writes first. */
ZTEST(fifo_decode, test_accel_gyro_order)
{
	const struct fifo_anchor anchor = {.ts_us = 1000000u, .index = 3};

	fill_pairs(4, false);
	zassert_equal(fifo_decode(s_rows, 8, &s_cfg, &anchor, &s_st, collect, NULL), 4);
	check_four_samples();
}

/* (g) a sample newer than the anchor gets a positive offset. */
ZTEST(fifo_decode, test_sample_after_the_anchor)
{
	const struct fifo_anchor anchor = {.ts_us = 1000000u, .index = 3};

	fill_pairs(5, true);
	zassert_equal(fifo_decode(s_rows, 10, &s_cfg, &anchor, &s_st, collect, NULL), 5);
	zassert_equal(s_out[3].timestamp_us, 1000000u);
	zassert_equal(s_out[4].timestamp_us, 1001042u);
}

/* (h) the 32-bit microsecond value wraps; differences stay right. */
ZTEST(fifo_decode, test_timestamp_wraps)
{
	const struct fifo_anchor anchor = {.ts_us = 1000u, .index = 3};

	fill_pairs(4, true);
	(void)fifo_decode(s_rows, 8, &s_cfg, &anchor, &s_st, collect, NULL);
	zassert_equal(s_out[0].timestamp_us, 4294965171u);
	zassert_equal(anchor.ts_us - s_out[0].timestamp_us, 3125u);
}

/* (c) a row without its partner at the end of the burst is counted. */
ZTEST(fifo_decode, test_odd_row_is_an_orphan)
{
	const struct fifo_anchor anchor = {.ts_us = 1000000u, .index = 2};

	fill_pairs(4, true);
	zassert_equal(fifo_decode(s_rows, 7, &s_cfg, &anchor, &s_st, collect, NULL), 3);
	zassert_equal(s_st.orphans, 1);
}

/* (d) an unknown tag between two pairs is skipped and counted. */
ZTEST(fifo_decode, test_unknown_tag_is_skipped)
{
	const struct fifo_anchor anchor = {.ts_us = 1000000u, .index = 1};

	put_row(s_rows, 0, TAG_GYRO, 200, 0, 0);
	put_row(s_rows, 1, TAG_ACCEL, 100, 0, 0);
	put_row(s_rows, 2, TAG_TIMESTAMP, 1, 2, 3);
	put_row(s_rows, 3, TAG_GYRO, 201, 0, 0);
	put_row(s_rows, 4, TAG_ACCEL, 101, 0, 0);

	zassert_equal(fifo_decode(s_rows, 5, &s_cfg, &anchor, &s_st, collect, NULL), 2);
	zassert_equal(s_st.unknown_tags, 1);
	zassert_equal(s_st.orphans, 0);
	zassert_within(s_out[1].accel.x, 101.0f, 1e-3f);
}

/* (e) the discontinuity mark goes on the first sample only and is consumed. */
ZTEST(fifo_decode, test_discontinuity_marks_first_sample)
{
	const struct fifo_anchor anchor = {.ts_us = 1000000u, .index = 3};

	fill_pairs(4, true);
	s_st.mark_next = true;
	(void)fifo_decode(s_rows, 8, &s_cfg, &anchor, &s_st, collect, NULL);

	zassert_equal(s_out[0].flags, IMU_SAMPLE_DISCONTINUITY);
	zassert_equal(s_out[1].flags, 0);
	zassert_equal(s_out[3].flags, 0);
	zassert_false(s_st.mark_next);
}

/* (g) the period follows the rate the sensor delivers: 234.696 Hz is a
 * nominal 240 Hz with the rate calibration at -17 steps; one period is
 * 4260.8 us, and 126 periods are 536865 us, not the nominal 525000. */
ZTEST(fifo_decode, test_period_from_delivered_rate)
{
	const struct fifo_anchor anchor = {.ts_us = 10000u, .index = 1};
	const struct fifo_decode_cfg cfg = {
		.accel_scale = 1.0f,
		.gyro_scale = 1.0f,
		.rate_mhz = 234696,
	};

	fill_pairs(3, true);
	zassert_equal(fifo_decode(s_rows, 6, &cfg, &anchor, &s_st, collect, NULL), 3);
	zassert_equal(s_out[0].timestamp_us, 5739u);
	zassert_equal(s_out[1].timestamp_us, 10000u);
	zassert_equal(s_out[2].timestamp_us, 14261u);
}

/* (f) scale factors: 70 mdps per LSB for the gyro, the accel table, and
 * signed little-endian unpacking. */
ZTEST(fifo_decode, test_scale_factors)
{
	const struct fifo_anchor anchor = {.ts_us = 0u, .index = 0};
	const struct fifo_decode_cfg cfg = {
		.accel_scale = fifo_accel_scale(2),
		.gyro_scale = FIFO_GYRO_SCALE_2000DPS,
		.rate_mhz = 240000,
	};

	put_row(s_rows, 0, TAG_GYRO, 1000, -1000, 0);
	put_row(s_rows, 1, TAG_ACCEL, 16393, 0, -16393);
	zassert_equal(fifo_decode(s_rows, 2, &cfg, &anchor, &s_st, collect, NULL), 1);

	zassert_within(s_out[0].gyro.x, 1.2217f, 1.2217f * 1e-3f);
	zassert_within(s_out[0].gyro.y, -1.2217f, 1.2217f * 1e-3f);
	/* 16393 LSB at 0.061 mg per LSB is 1 g */
	zassert_within(s_out[0].accel.x, 9.80665f, 0.01f);
	zassert_within(s_out[0].accel.z, -9.80665f, 0.01f);

	zassert_equal(fifo_accel_scale(3), 0.0f);
	zassert_within(fifo_accel_scale(16) / fifo_accel_scale(2), 8.0f, 1e-3f);
}

ZTEST_SUITE(fifo_decode, NULL, NULL, before, NULL, NULL);
