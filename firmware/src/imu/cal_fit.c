/* cal_fit.c - arithmetic of the gyro bias run and the magnetometer hard and
 * soft iron fit. Owns: nothing static. Runs on: the caller (burst thread,
 * app_wq, host test). Pure: no platform header, single precision only. */
#include "imu/cal_fit.h"
#include <errno.h>
#include <math.h>

void cal_gyro_add(struct cal_gyro_acc *acc, const float g[3])
{
	for (int i = 0; i < 3; i++) {
		acc->sum[i] += g[i];
		acc->sum_sq[i] += g[i] * g[i];
	}
	acc->n++;
}

int cal_gyro_finish(const struct cal_gyro_acc *acc, float bias[3], float *worst_std)
{
	if (acc->n == 0u) {
		return -ENODATA;
	}

	const float inv_n = 1.0f / (float)acc->n;
	float worst_var = 0.0f;

	for (int i = 0; i < 3; i++) {
		const float mean = acc->sum[i] * inv_n;
		const float var = acc->sum_sq[i] * inv_n - mean * mean;

		bias[i] = mean;
		/* rounding can leave a constant input slightly below zero */
		if (var > worst_var) {
			worst_var = var;
		}
	}
	*worst_std = sqrtf(worst_var);
	return 0;
}

int cal_mag_fit(const float min[3], const float max[3], float min_radius, float bias[3],
		float scale[3])
{
	float radius[3];
	float mean_radius = 0.0f;

	for (int i = 0; i < 3; i++) {
		radius[i] = (max[i] - min[i]) * 0.5f;
		/* written so that a NaN fails the test too */
		if (!(radius[i] >= min_radius) || !(radius[i] > 0.0f)) {
			return -ENODATA;
		}
		mean_radius += radius[i];
	}
	mean_radius /= 3.0f;

	for (int i = 0; i < 3; i++) {
		bias[i] = (max[i] + min[i]) * 0.5f;
		scale[i] = mean_radius / radius[i];
	}
	return 0;
}
