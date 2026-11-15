/*
 * cal_fit.h - arithmetic of the two calibration procedures.
 * No Zephyr include: this header is shared with the host tests.
 */
#ifndef IMU_CAL_FIT_H_
#define IMU_CAL_FIT_H_
#include <stdint.h>

struct cal_gyro_acc {
	float sum[3];
	float sum_sq[3];
	uint32_t n;
};

/** Called from any context; pure. Adds one raw gyro sample, rad/s, to the
 *  running sums. */
void cal_gyro_add(struct cal_gyro_acc *acc, const float g[3]);

/** Called from any context; pure.
 *  @return 0 and the mean per axis plus the largest standard deviation of
 *  the three axes, or -ENODATA when acc->n is 0. */
int cal_gyro_finish(const struct cal_gyro_acc *acc, float bias[3], float *worst_std);

/** Called from any context; pure. bias = (max + min) / 2 per axis,
 *  scale = mean radius / radius with radius = (max - min) / 2.
 *  @return 0, or -ENODATA when a radius is below min_radius. */
int cal_mag_fit(const float min[3], const float max[3], float min_radius, float bias[3],
		float scale[3]);

#endif /* IMU_CAL_FIT_H_ */
