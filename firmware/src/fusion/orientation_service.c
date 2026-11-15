/* orientation_service.c - Owns: the filter state (burst thread only), the
 * published result under s_lock, the reset request. Runs on: the burst
 * thread (update) and any reader (get, request). The filter step runs
 * outside any lock; other threads never touch the filter, they send a
 * request. No Euler angles here: a reader converts its own copy.
 *
 * The filter does not start at the identity. Converging from there takes up
 * to 15 s at beta 0.1 and tilts the estimate on the way (wiki chapter 10,
 * correction note). The first sample after boot or a reset sets tilt and
 * heading directly; the filter only tracks from then on. */
#include "fusion/orientation_service.h"
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include "fusion/madgwick.h"

/* static state */
static madgwick_t s_filter = {.q0 = 1.0f}; /* burst thread only */
static bool s_align = true;                /* burst thread only: the next sample sets q */
static bool s_need_heading;                /* burst thread only: aligned without a field */
static struct k_spinlock s_lock;
static imu_orientation_t s_result;         /* under s_lock: six words read as one set */
static atomic_t s_reset_req;
#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
static atomic_t s_identity_req;
#endif

/* helpers: trust the caller */

/* Sets q from the sample when an alignment is due. Tilt comes from the
 * accelerometer, heading from the field. Without a field the heading is set
 * later, by the first step that has one, with the tilt the filter holds. */
static void align_if_due(const imu_sample_t *s, const imu_vec3_t *mag)
{
	if (s_align) {
		const int rc = (mag != NULL)
				       ? madgwick_align(&s_filter, s->accel.x, s->accel.y,
							s->accel.z, mag->x, mag->y, mag->z)
				       : madgwick_align(&s_filter, s->accel.x, s->accel.y,
							s->accel.z, 0.0f, 0.0f, 0.0f);

		if (rc == 0) { /* a zero accel vector: try again at the next sample */
			s_align = false;
			s_need_heading = (mag == NULL);
		}
	} else if (s_need_heading && mag != NULL) {
		const float q0 = s_filter.q0, q1 = s_filter.q1, q2 = s_filter.q2, q3 = s_filter.q3;

		/* up in the body frame, as the filter sees it */
		(void)madgwick_align(&s_filter, 2.0f * (q1 * q3 - q0 * q2),
				     2.0f * (q0 * q1 + q2 * q3), 1.0f - 2.0f * (q1 * q1 + q2 * q2),
				     mag->x, mag->y, mag->z);
		s_need_heading = false;
	}
}

/* public API */

void orientation_service_update(const imu_sample_t *s, const imu_vec3_t *gyro,
				const imu_vec3_t *mag, float dt, float beta, uint8_t flags)
{
	if (atomic_set(&s_reset_req, 0) != 0) { /* returns the old value: test and clear */
		s_align = true;
	}
#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
	if (atomic_set(&s_identity_req, 0) != 0) {
		madgwick_reset(&s_filter);
		s_align = false;
		s_need_heading = false;
	}
#endif
	align_if_due(s, mag);
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

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
void orientation_service_request_identity(void)
{
	atomic_set(&s_identity_req, 1);
}
#endif
