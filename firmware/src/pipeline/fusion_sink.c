/* fusion_sink.c - Owns: the cached dt, beta and mode (burst thread only), the
 * counters, the bench instruments. Runs on: the burst thread (fusion_sink)
 * and app_wq (the other functions). Sink contract: no sleep, no lock another
 * thread can hold for long per sample, no log line; the stats item prints
 * from the getters. The configuration mutex is taken only on a discontinuity
 * or a configuration change. */
#include "pipeline/fusion_sink.h"
#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#ifdef CONFIG_TIMING_FUNCTIONS
#include <zephyr/timing/timing.h>
#endif
#include "app/app_config.h"
#include "fusion/orientation_service.h"
#include "imu/imu_calibration.h"
#include "imu/mag_poll.h"

#define MAG_MAX_AGE_US 300000          /* three missed polls */
#define MAG_NORM2_MIN  (20.0f * 20.0f) /* uT squared; the earth field is 25 to 65 uT */
#define MAG_NORM2_MAX  (80.0f * 80.0f)
#define RAD_TO_CDEG    5729.578f       /* hundredths of a degree per radian */

/* static state */
static atomic_t s_cfg_dirty = ATOMIC_INIT(1);
static atomic_t s_updates, s_gaps, s_mag_rejected;

static float s_dt;     /* burst thread only; seconds */
static float s_beta;   /* burst thread only; rad/s */
static uint8_t s_mode; /* burst thread only; fusion_mode_t */

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
static float s_turn_frac;      /* burst thread only; the part below 0.01 degrees */
static atomic_t s_turn_z_cdeg;
#endif

#ifdef CONFIG_TIMING_FUNCTIONS
static atomic_t s_step_min_ns = ATOMIC_INIT(INT32_MAX);
static atomic_t s_step_last_ns;
static atomic_t s_step_max_ns;
#endif

/* public API */

void fusion_sink(const imu_sample_t *s)
{
	const bool gap = (s->flags & IMU_SAMPLE_DISCONTINUITY) != 0u;
	imu_vec3_t gyro = s->gyro; /* the raw stream and the DSP window keep the raw value */
	const imu_vec3_t *mag = NULL;
	imu_mag_sample_t m;
	uint8_t flags = 0;

	/* A gap is the one moment the rate can have changed: the flush marks it. */
	if (gap || atomic_get(&s_cfg_dirty) != 0) {
		app_config_t cfg;

		/* before the copy: a change after it sets the flag again */
		atomic_set(&s_cfg_dirty, 0);
		app_config_snapshot(&cfg);
		s_dt = 1.0f / (float)cfg.imu_odr_hz;
		s_beta = (float)cfg.fusion_beta_x1000 * 0.001f;
		s_mode = (uint8_t)cfg.fusion_mode;
	}
	if (gap) {
		atomic_inc(&s_gaps);
	}

	imu_cal_gyro_feed(&gyro, s_dt, gap); /* collector: raw values, also with fusion off */
	if (s_mode == FUSION_MODE_OFF) {
		return;
	}
	imu_cal_apply_gyro(&gyro);

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
	s_turn_frac += gyro.z * s_dt * RAD_TO_CDEG;
	const int32_t whole = (int32_t)s_turn_frac;

	if (whole != 0) {
		s_turn_frac -= (float)whole;
		atomic_add(&s_turn_z_cdeg, whole);
	}
#endif

	if (s_mode == FUSION_MODE_9AXIS) {
		mag_poll_get(&m);
		const float n2 = m.mag.x * m.mag.x + m.mag.y * m.mag.y + m.mag.z * m.mag.z;

		/* Signed difference of wrapping timestamps: negative when the
		 * poll is newer than the sample, which counts as fresh. */
		if (m.valid && (int32_t)(s->timestamp_us - m.timestamp_us) < MAG_MAX_AGE_US &&
		    n2 > MAG_NORM2_MIN && n2 < MAG_NORM2_MAX) {
			mag = &m.mag;
		} else {
			flags |= ORIENT_MAG_REJECTED;
			atomic_inc(&s_mag_rejected);
		}
	}

#ifdef CONFIG_TIMING_FUNCTIONS
	timing_t t0 = timing_counter_get();
#endif
	orientation_service_update(s, &gyro, mag, s_dt, s_beta, flags);
#ifdef CONFIG_TIMING_FUNCTIONS
	timing_t t1 = timing_counter_get();
	const atomic_val_t ns = (atomic_val_t)timing_cycles_to_ns(timing_cycles_get(&t0, &t1));

	atomic_set(&s_step_last_ns, ns);
	if (ns < atomic_get(&s_step_min_ns)) {
		atomic_set(&s_step_min_ns, ns);
	}
	if (ns > atomic_get(&s_step_max_ns)) {
		atomic_set(&s_step_max_ns, ns);
	}
#endif
	atomic_inc(&s_updates);
}

void fusion_sink_config_changed(void)
{
	atomic_set(&s_cfg_dirty, 1);
}

void fusion_sink_get_stats(struct fusion_sink_stats *out)
{
	out->updates = (uint32_t)atomic_get(&s_updates);
	out->gaps = (uint32_t)atomic_get(&s_gaps);
	out->mag_rejected = (uint32_t)atomic_get(&s_mag_rejected);
}

#ifdef CONFIG_IMU_TAG_DEBUG_COUNTERS
int32_t fusion_sink_turn_z_cdeg(void)
{
	return (int32_t)atomic_get(&s_turn_z_cdeg);
}
#endif

#ifdef CONFIG_TIMING_FUNCTIONS
void fusion_sink_step_ns_take(uint32_t *min_ns, uint32_t *last_ns, uint32_t *max_ns)
{
	const atomic_val_t min = atomic_set(&s_step_min_ns, INT32_MAX);

	*max_ns = (uint32_t)atomic_set(&s_step_max_ns, 0);
	*last_ns = (uint32_t)atomic_get(&s_step_last_ns);
	*min_ns = (min == INT32_MAX) ? 0u : (uint32_t)min;
	if (min == INT32_MAX) {
		*last_ns = 0u;
	}
}
#endif
