/* madgwick.c - Madgwick orientation filter. Owns: nothing static; arithmetic
 * on a madgwick_t. Runs on: the caller (burst thread, host test). Pure: no
 * clock, no lock, no log, no platform header, so the same file is compiled
 * into the host test. Every constant and call is single precision.
 *
 * Gradient-descent form after S. O. H. Madgwick. The correction is a step of
 * fixed length: the gradient is normalised and scaled by beta, so accel and
 * mag together turn the estimate by at most 2 * beta rad/s. */
#include "fusion/madgwick.h"
#include <errno.h>
#include <math.h>
#include <stdbool.h>

/* helpers: trust the caller */

/* 0 for a zero, negative or non-finite argument: a zero vector then gives a
 * zero correction without a division. */
static float inv_sqrt(float x)
{
	return (x > 0.0f && isfinite(x)) ? 1.0f / sqrtf(x) : 0.0f;
}

/* public API */

void madgwick_init(madgwick_t *f, float beta)
{
	madgwick_reset(f);
	f->beta = beta;
}

void madgwick_reset(madgwick_t *f)
{
	f->q0 = 1.0f;
	f->q1 = 0.0f;
	f->q2 = 0.0f;
	f->q3 = 0.0f;
}

int madgwick_align(madgwick_t *f, float ux, float uy, float uz, float mx, float my, float mz)
{
	const float inv_u = inv_sqrt(ux * ux + uy * uy + uz * uz);

	if (!(inv_u > 0.0f)) {
		return -EINVAL;
	}
	ux *= inv_u;
	uy *= inv_u;
	uz *= inv_u;

	/* North in the body frame: the horizontal part of the field. */
	const float m2 = mx * mx + my * my + mz * mz;
	const float d = mx * ux + my * uy + mz * uz;
	float nx = mx - d * ux, ny = my - d * uy, nz = mz - d * uz;
	const float n2 = nx * nx + ny * ny + nz * nz;

	/* No field, or a field within about 0.6 degrees of vertical, gives no
	 * heading: take the horizontal part of body x, or of body y when x is
	 * closer to vertical. */
	if (!(n2 > 1e-4f * m2) || !isfinite(n2)) {
		const bool x_vertical = ux * ux > 0.5f;
		const float e = x_vertical ? uy : ux;

		nx = (x_vertical ? 0.0f : 1.0f) - e * ux;
		ny = (x_vertical ? 1.0f : 0.0f) - e * uy;
		nz = -e * uz;
	}

	const float inv_n = inv_sqrt(nx * nx + ny * ny + nz * nz);

	nx *= inv_n;
	ny *= inv_n;
	nz *= inv_n;

	/* West completes the right-handed frame: up cross north. */
	const float wx = uy * nz - uz * ny;
	const float wy = uz * nx - ux * nz;
	const float wz = ux * ny - uy * nx;

	/* The rows of the body-to-earth matrix are north, west and up in body
	 * coordinates. Matrix to quaternion, largest component first, so the
	 * divisor is never small. */
	const float tr = nx + wy + uz;
	float q0, q1, q2, q3;

	if (tr > 0.0f) {
		const float s = 2.0f * sqrtf(tr + 1.0f);

		q0 = 0.25f * s;
		q1 = (uy - wz) / s;
		q2 = (nz - ux) / s;
		q3 = (wx - ny) / s;
	} else if (nx > wy && nx > uz) {
		const float s = 2.0f * sqrtf(1.0f + nx - wy - uz);

		q0 = (uy - wz) / s;
		q1 = 0.25f * s;
		q2 = (ny + wx) / s;
		q3 = (nz + ux) / s;
	} else if (wy > uz) {
		const float s = 2.0f * sqrtf(1.0f + wy - nx - uz);

		q0 = (nz - ux) / s;
		q1 = (ny + wx) / s;
		q2 = 0.25f * s;
		q3 = (wz + uy) / s;
	} else {
		const float s = 2.0f * sqrtf(1.0f + uz - nx - wy);

		q0 = (wx - ny) / s;
		q1 = (nz + ux) / s;
		q2 = (wz + uy) / s;
		q3 = 0.25f * s;
	}

	const float inv_q = inv_sqrt(q0 * q0 + q1 * q1 + q2 * q2 + q3 * q3);

	if (!(inv_q > 0.0f)) {
		return -EINVAL;
	}
	/* q and -q are the same rotation: keep w positive */
	const float sign = (q0 < 0.0f) ? -inv_q : inv_q;

	f->q0 = q0 * sign;
	f->q1 = q1 * sign;
	f->q2 = q2 * sign;
	f->q3 = q3 * sign;
	return 0;
}

int madgwick_update_6axis(madgwick_t *f, float gx, float gy, float gz, float ax, float ay,
			  float az, float dt)
{
	const float q0 = f->q0, q1 = f->q1, q2 = f->q2, q3 = f->q3;
	float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;

	/* written so that a NaN fails the test too */
	if (!(dt > 0.0f) || !isfinite(dt)) {
		return -EINVAL;
	}

	const float inv_a = inv_sqrt(ax * ax + ay * ay + az * az);

	if (inv_a > 0.0f) {
		/* predicted minus measured direction of gravity */
		const float f0 = 2.0f * (q1 * q3 - q0 * q2) - ax * inv_a;
		const float f1 = 2.0f * (q0 * q1 + q2 * q3) - ay * inv_a;
		const float f2 = 1.0f - 2.0f * (q1 * q1 + q2 * q2) - az * inv_a;

		/* gradient J^T f */
		s0 = -2.0f * q2 * f0 + 2.0f * q1 * f1;
		s1 = 2.0f * q3 * f0 + 2.0f * q0 * f1 - 4.0f * q1 * f2;
		s2 = -2.0f * q0 * f0 + 2.0f * q3 * f1 - 4.0f * q2 * f2;
		s3 = 2.0f * q1 * f0 + 2.0f * q2 * f1;

		const float inv_s = inv_sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);

		s0 *= inv_s;
		s1 *= inv_s;
		s2 *= inv_s;
		s3 *= inv_s;
	}

	/* gyro rate minus the correction, integrated over one sample period */
	const float n0 = q0 + (0.5f * (-q1 * gx - q2 * gy - q3 * gz) - f->beta * s0) * dt;
	const float n1 = q1 + (0.5f * (q0 * gx + q2 * gz - q3 * gy) - f->beta * s1) * dt;
	const float n2 = q2 + (0.5f * (q0 * gy - q1 * gz + q3 * gx) - f->beta * s2) * dt;
	const float n3 = q3 + (0.5f * (q0 * gz + q1 * gy - q2 * gx) - f->beta * s3) * dt;
	const float inv_q = inv_sqrt(n0 * n0 + n1 * n1 + n2 * n2 + n3 * n3);

	/* a step that gave a zero or non-finite quaternion leaves the old one */
	if (inv_q > 0.0f) {
		f->q0 = n0 * inv_q;
		f->q1 = n1 * inv_q;
		f->q2 = n2 * inv_q;
		f->q3 = n3 * inv_q;
	}
	return 0;
}

int madgwick_update_9axis(madgwick_t *f, float gx, float gy, float gz, float ax, float ay,
			  float az, float mx, float my, float mz, float dt)
{
	const float q0 = f->q0, q1 = f->q1, q2 = f->q2, q3 = f->q3;

	if (!(dt > 0.0f) || !isfinite(dt)) {
		return -EINVAL;
	}

	const float inv_a = inv_sqrt(ax * ax + ay * ay + az * az);
	const float inv_m = inv_sqrt(mx * mx + my * my + mz * mz);

	if (!(inv_a > 0.0f) || !(inv_m > 0.0f)) {
		/* no usable accel or mag direction: the 6-axis step covers both */
		return madgwick_update_6axis(f, gx, gy, gz, ax, ay, az, dt);
	}
	ax *= inv_a;
	ay *= inv_a;
	az *= inv_a;
	mx *= inv_m;
	my *= inv_m;
	mz *= inv_m;

	const float q0q0 = q0 * q0, q0q1 = q0 * q1, q0q2 = q0 * q2, q0q3 = q0 * q3;
	const float q1q1 = q1 * q1, q1q2 = q1 * q2, q1q3 = q1 * q3;
	const float q2q2 = q2 * q2, q2q3 = q2 * q3, q3q3 = q3 * q3;

	/* The measured field in the earth frame, as q sees it. */
	const float hx = mx * (q0q0 + q1q1 - q2q2 - q3q3) + 2.0f * my * (q1q2 - q0q3) +
			 2.0f * mz * (q1q3 + q0q2);
	const float hy = 2.0f * mx * (q1q2 + q0q3) + my * (q0q0 - q1q1 + q2q2 - q3q3) +
			 2.0f * mz * (q2q3 - q0q1);
	const float hz = 2.0f * mx * (q1q3 - q0q2) + 2.0f * my * (q2q3 + q0q1) +
			 mz * (q0q0 - q1q1 - q2q2 + q3q3);

	/* Reference field b = (bx, 0, bz): the vertical part as measured, the
	 * horizontal part put on the x axis. The gradient terms below use twice
	 * b; with the plain values the filter compares against half the field
	 * and converges two to six times slower (host test, reference case). */
	const float _2bx = 2.0f * sqrtf(hx * hx + hy * hy);
	const float _2bz = 2.0f * hz;
	const float _4bx = 2.0f * _2bx;
	const float _4bz = 2.0f * _2bz;

	/* predicted minus measured: gravity (g) and field (b) */
	const float fg0 = 2.0f * (q1q3 - q0q2) - ax;
	const float fg1 = 2.0f * (q0q1 + q2q3) - ay;
	const float fg2 = 1.0f - 2.0f * (q1q1 + q2q2) - az;
	const float fb0 = _2bx * (0.5f - q2q2 - q3q3) + _2bz * (q1q3 - q0q2) - mx;
	const float fb1 = _2bx * (q1q2 - q0q3) + _2bz * (q0q1 + q2q3) - my;
	const float fb2 = _2bx * (q0q2 + q1q3) + _2bz * (0.5f - q1q1 - q2q2) - mz;

	/* sum of both gradients J^T f */
	float s0 = -2.0f * q2 * fg0 + 2.0f * q1 * fg1 - _2bz * q2 * fb0 +
		   (-_2bx * q3 + _2bz * q1) * fb1 + _2bx * q2 * fb2;
	float s1 = 2.0f * q3 * fg0 + 2.0f * q0 * fg1 - 4.0f * q1 * fg2 + _2bz * q3 * fb0 +
		   (_2bx * q2 + _2bz * q0) * fb1 + (_2bx * q3 - _4bz * q1) * fb2;
	float s2 = -2.0f * q0 * fg0 + 2.0f * q3 * fg1 - 4.0f * q2 * fg2 +
		   (-_4bx * q2 - _2bz * q0) * fb0 + (_2bx * q1 + _2bz * q3) * fb1 +
		   (_2bx * q0 - _4bz * q2) * fb2;
	float s3 = 2.0f * q1 * fg0 + 2.0f * q2 * fg1 + (-_4bx * q3 + _2bz * q1) * fb0 +
		   (-_2bx * q0 + _2bz * q2) * fb1 + _2bx * q1 * fb2;
	const float inv_s = inv_sqrt(s0 * s0 + s1 * s1 + s2 * s2 + s3 * s3);

	s0 *= inv_s;
	s1 *= inv_s;
	s2 *= inv_s;
	s3 *= inv_s;

	const float n0 = q0 + (0.5f * (-q1 * gx - q2 * gy - q3 * gz) - f->beta * s0) * dt;
	const float n1 = q1 + (0.5f * (q0 * gx + q2 * gz - q3 * gy) - f->beta * s1) * dt;
	const float n2 = q2 + (0.5f * (q0 * gy - q1 * gz + q3 * gx) - f->beta * s2) * dt;
	const float n3 = q3 + (0.5f * (q0 * gz + q1 * gy - q2 * gx) - f->beta * s3) * dt;
	const float inv_q = inv_sqrt(n0 * n0 + n1 * n1 + n2 * n2 + n3 * n3);

	if (inv_q > 0.0f) {
		f->q0 = n0 * inv_q;
		f->q1 = n1 * inv_q;
		f->q2 = n2 * inv_q;
		f->q3 = n3 * inv_q;
	}
	return 0;
}
