/*
 * orientation_service.h - the filter state and the published orientation.
 * Context: the burst thread owns the filter and runs one step per sample;
 * any thread reads the result as a copy under a spinlock or requests a
 * reset. Units: gyro in rad/s, dt in seconds.
 */
#ifndef FUSION_ORIENTATION_SERVICE_H_
#define FUSION_ORIENTATION_SERVICE_H_
#include <stdint.h>
#include "imu/imu_types.h"

/** Called from the burst thread only. One filter step with the sample's
 *  accel and the given (bias-corrected) gyro, then publish. mag is NULL for
 *  a 6-axis step. flags are ORIENT_* bits the caller adds to the result. */
void orientation_service_update(const imu_sample_t *s, const imu_vec3_t *gyro,
				const imu_vec3_t *mag, float dt, float beta, uint8_t flags);

/** Called from any thread; copies six words with interrupts masked.
 *  out->flags has ORIENT_VALID once the first step has run. */
void orientation_service_get(imu_orientation_t *out);

/** Called from any thread; one atomic store. The burst thread aligns the
 *  filter again at its next sample: tilt from the accelerometer, heading
 *  from the magnetometer when a usable sample exists, as after boot. */
void orientation_service_request_reset(void);

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
/** Called from any thread; bench instrument. The burst thread sets the
 *  filter to the identity at its next sample without aligning, so the
 *  correction rate can be watched (docs/MEASUREMENTS.md M-10-5). */
void orientation_service_request_identity(void);
#endif

#endif /* FUSION_ORIENTATION_SERVICE_H_ */
