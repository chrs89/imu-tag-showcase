/*
 * fifo_decode.h - turns LSM6DSV16X FIFO rows into samples.
 * Pure: no Zephyr include, no clock, no I/O. Runs on the burst thread and in
 * the host tests. A row is 7 bytes: one tag byte, then X, Y, Z as signed
 * 16-bit little-endian. Two rows, one gyro and one accel, make a sample.
 */
#ifndef FIFO_DECODE_H_
#define FIFO_DECODE_H_
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "imu/imu_types.h"

#define FIFO_ROW_BYTES 7u

/* 70 mdps per LSB at the 2000 dps setting (ST HAL sensitivity), in rad/s. */
#define FIFO_GYRO_SCALE_2000DPS 1.2217305e-3f

struct fifo_decode_cfg {
	float accel_scale; /* m/s2 per LSB, from fifo_accel_scale() */
	float gyro_scale;  /* rad/s per LSB */
	uint32_t rate_mhz; /* sample rate in millihertz, as the sensor delivers it
			    * (imu_ext_rate_mhz()); sets the period between timestamps */
};

/* The one known point in time of a burst. */
struct fifo_anchor {
	uint32_t ts_us; /* time of the sample with number index */
	uint16_t index; /* 0 is the oldest sample of the burst */
};

/* Owned by the caller, survives across bursts. */
struct fifo_decode_state {
	uint8_t seq;
	bool mark_next;        /* next sample gets IMU_SAMPLE_DISCONTINUITY */
	uint32_t orphans;      /* rows without a partner at the end of a burst */
	uint32_t unknown_tags; /* rows that are neither gyro nor accel */
};

typedef void (*fifo_sample_fn)(const imu_sample_t *s, void *user);

/** Called from the burst thread, or from a test. Pairs the rows, scales them
 *  and calls @p on_sample once per sample, oldest first. Sample k gets
 *  anchor->ts_us plus (k - anchor->index) sample periods.
 *  @return number of samples passed to on_sample; 0 if cfg->rate_mhz is 0. */
size_t fifo_decode(const uint8_t *rows, uint16_t n_rows, const struct fifo_decode_cfg *cfg,
		   const struct fifo_anchor *anchor, struct fifo_decode_state *st,
		   fifo_sample_fn on_sample, void *user);

/** Called from any context.
 *  @return m/s2 per LSB for a range of 2, 4, 8 or 16 g; 0 for any other value. */
float fifo_accel_scale(uint8_t range_g);

#endif /* FIFO_DECODE_H_ */
