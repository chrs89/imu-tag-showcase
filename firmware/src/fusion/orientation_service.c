/* orientation_service.c - Owns: the filter state (burst thread only), the
 * published result under s_lock, the reset request. Runs on: the burst
 * thread (update) and any reader (get, request). The filter step runs
 * outside any lock; other threads never touch the filter, they send a
 * request. No Euler angles here: a reader converts its own copy. */
#include "fusion/orientation_service.h"
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include "fusion/madgwick.h"

/* static state */
static madgwick_t s_filter = {.q0 = 1.0f}; /* burst thread only */
static struct k_spinlock s_lock;
static imu_orientation_t s_result;         /* under s_lock: six words read as one set */
static atomic_t s_reset_req;

/* public API */

void orientation_service_update(const imu_sample_t *s, const imu_vec3_t *gyro,
				const imu_vec3_t *mag, float dt, float beta, uint8_t flags)
{
	if (atomic_set(&s_reset_req, 0) != 0) { /* returns the old value: test and clear */
		madgwick_reset(&s_filter);
	}
	s_filter.beta = beta;

	/* dt comes from the sample rate and is positive, so the steps cannot fail */
	if (mag != NULL) {
		(void)madgwick_update_9axis(&s_filter, gyro->x, gyro->y, gyro->z, s->accel.x,
					    s->accel.y, s->accel.z, mag->x, mag->y, mag->z, dt);
		flags |= ORIENT_USED_MAG;
	} else {
		(void)madgwick_update_6axis(&s_filter, gyro->x, gyro->y, gyro->z, s->accel.x,
					    s->accel.y, s->accel.z, dt);
	}

	const imu_orientation_t r = {
		.w = s_filter.q0,
		.x = s_filter.q1,
		.y = s_filter.q2,
		.z = s_filter.q3,
		.timestamp_us = s->timestamp_us,
		.seq = s->seq,
		.flags = flags | ORIENT_VALID,
	};
	k_spinlock_key_t key = k_spin_lock(&s_lock);

	s_result = r;
	k_spin_unlock(&s_lock, key);
}

void orientation_service_get(imu_orientation_t *out)
{
	k_spinlock_key_t key = k_spin_lock(&s_lock);

	*out = s_result;
	k_spin_unlock(&s_lock, key);
}

void orientation_service_request_reset(void)
{
	atomic_set(&s_reset_req, 1);
}
