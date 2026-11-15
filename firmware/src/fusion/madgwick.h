/*
 * madgwick.h - Madgwick orientation filter, 6-axis and 9-axis.
 * No Zephyr include: this header is shared with the host tests.
 *
 * Quaternion: q = (q0, q1, q2, q3) = (w, x, y, z), rotates a vector from the
 * body frame into the earth frame. Earth frame: x to magnetic north, y to
 * west, z up. In 6-axis mode x is the heading at the start.
 * Units: gyro in rad/s, dt in seconds; accel and mag in any unit, only their
 * direction is used.
 */
#ifndef FUSION_MADGWICK_H_
#define FUSION_MADGWICK_H_

typedef struct {
	float q0, q1, q2, q3; /* w, x, y, z: body to earth */
	float beta;           /* rad/s of q; the correction turns q by at most 2 * beta */
} madgwick_t;

/** Called from any context; pure. Sets the identity quaternion and the gain. */
void madgwick_init(madgwick_t *f, float beta);

/** Called from any context; pure. Sets the identity quaternion, keeps the gain. */
void madgwick_reset(madgwick_t *f);

/** Called from the burst thread, or from a test. Pure. A zero accel vector
 *  gives a gyro-only step.
 *  @return 0, or -EINVAL when dt is not a positive finite number. */
int madgwick_update_6axis(madgwick_t *f, float gx, float gy, float gz, float ax, float ay,
			  float az, float dt);

/** Called from the burst thread, or from a test. Pure. A zero mag vector
 *  gives the 6-axis step, so a caller without a magnetometer sample can pass
 *  zeros.
 *  @return 0, or -EINVAL when dt is not a positive finite number. */
int madgwick_update_9axis(madgwick_t *f, float gx, float gy, float gz, float ax, float ay,
			  float az, float mx, float my, float mz, float dt);

#endif /* FUSION_MADGWICK_H_ */
