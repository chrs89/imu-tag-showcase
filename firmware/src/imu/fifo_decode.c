/* fifo_decode.c - FIFO rows to samples. Owns: nothing static. Runs on: the
 * burst thread and the host tests. Pure arithmetic on the buffer it is given;
 * the pairing state that survives a burst lives in the caller's
 * struct fifo_decode_state. */
#include "imu/fifo_decode.h"

/* Tag values in the upper five bits of byte 0 (ST HAL: LSM6DSV16X_GY_NC_TAG,
 * LSM6DSV16X_XL_NC_TAG). */
#define FIFO_TAG_GYRO  0x01u
#define FIFO_TAG_ACCEL 0x02u

/* m/s2 per LSB: ST HAL sensitivity in mg per LSB times 9.80665e-3. */
#define ACCEL_SCALE_2G  5.9820565e-4f /* 0.061 mg */
#define ACCEL_SCALE_4G  1.1964113e-3f /* 0.122 mg */
#define ACCEL_SCALE_8G  2.3928226e-3f /* 0.244 mg */
#define ACCEL_SCALE_16G 4.7856452e-3f /* 0.488 mg */

/* helpers: trust the caller */

/* Bytes 1 to 6 of a row: X, Y, Z as signed 16-bit little-endian. */
static void unpack(const uint8_t *row, int16_t v[3])
{
	for (int i = 0; i < 3; i++) {
		v[i] = (int16_t)((uint16_t)row[1 + 2 * i] | ((uint16_t)row[2 + 2 * i] << 8));
	}
}

/* n sample periods in microseconds, rounded to nearest; n may be negative.
 * Computed from the sample number, not accumulated, so the rounding of one
 * period does not add up over a burst. */
static int32_t period_offset_us(int32_t n, uint16_t odr_hz)
{
	const int32_t num = n * 1000000; /* |n| <= 255, fits in 32 bits */

	return (num >= 0 ? num + odr_hz / 2 : num - odr_hz / 2) / odr_hz;
}

/* public API: validate at the boundary */

size_t fifo_decode(const uint8_t *rows, uint16_t n_rows, const struct fifo_decode_cfg *cfg,
		   const struct fifo_anchor *anchor, struct fifo_decode_state *st,
		   fifo_sample_fn on_sample, void *user)
{
	bool have_a = false, have_g = false;
	int16_t a[3] = {0}, g[3] = {0};
	uint16_t k = 0;

	if (cfg->odr_hz == 0u) {
		return 0;
	}

	for (uint16_t i = 0; i < n_rows; i++) {
		const uint8_t *r = &rows[(size_t)i * FIFO_ROW_BYTES];
		const uint8_t tag = (r[0] >> 3) & 0x1Fu;

		if (tag == FIFO_TAG_ACCEL) {
			unpack(r, a);
			have_a = true;
		} else if (tag == FIFO_TAG_GYRO) {
			unpack(r, g);
			have_g = true;
		} else {
			st->unknown_tags++;
			continue;
		}
		if (!(have_a && have_g)) {
			continue;
		}

		const imu_sample_t s = {
			.accel = {(float)a[0] * cfg->accel_scale, (float)a[1] * cfg->accel_scale,
				  (float)a[2] * cfg->accel_scale},
			.gyro = {(float)g[0] * cfg->gyro_scale, (float)g[1] * cfg->gyro_scale,
				 (float)g[2] * cfg->gyro_scale},
			/* counted from the anchor sample: older negative, newer positive */
			.timestamp_us = anchor->ts_us +
					(uint32_t)period_offset_us((int32_t)k - (int32_t)anchor->index,
								   cfg->odr_hz),
			.seq = st->seq++,
			.flags = st->mark_next ? IMU_SAMPLE_DISCONTINUITY : 0u,
		};

		st->mark_next = false;
		on_sample(&s, user);
		have_a = false;
		have_g = false;
		k++;
	}
	if (have_a || have_g) {
		st->orphans++;
	}
	return k;
}

float fifo_accel_scale(uint8_t range_g)
{
	switch (range_g) {
	case 2:
		return ACCEL_SCALE_2G;
	case 4:
		return ACCEL_SCALE_4G;
	case 8:
		return ACCEL_SCALE_8G;
	case 16:
		return ACCEL_SCALE_16G;
	default:
		return 0.0f;
	}
}
