/* imu_calibration.c - calibration values and their store. Owns: gyro bias,
 * magnetometer bias and scale under s_cal_lock; the settings handler; the two
 * collectors. Runs on: the burst thread (apply, gyro collector), app_wq
 * (apply, magnetometer collector, start, finish, save) and the thread that
 * loads the settings (the handler; main at boot). The arithmetic is in
 * cal_fit.c. Never writes flash from the burst thread. */
#include "imu/imu_calibration.h"
#include <errno.h>
#include <float.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include "imu/cal_fit.h"

LOG_MODULE_REGISTER(imu_cal, LOG_LEVEL_INF);

#define CAL_VERSION        1u
#define CAL_GYRO_RUN_S     2.0f         /* counted as n * dt, so the same at every rate */
#define CAL_GYRO_MAX_STD   8.7266e-3f   /* 0.5 dps in rad/s: above it the tag was moved */
#define CAL_MAG_MIN_RADIUS 10.0f        /* uT; the earth field is 25 to 65 uT */

struct cal_gyro_rec {
	float bias[3];
	uint32_t version;
};

struct cal_mag_rec {
	float bias[3];
	float scale[3];
	uint32_t version;
};

/* static state */
static struct k_spinlock s_cal_lock;
static float s_gyro_bias[3];                    /* rad/s; under s_cal_lock */
static float s_mag_bias[3];                     /* uT; under s_cal_lock */
static float s_mag_scale[3] = {1.0f, 1.0f, 1.0f}; /* under s_cal_lock */

static atomic_t s_gyro_req;                     /* set on app_wq, taken by the burst thread */
static atomic_t s_gyro_state = ATOMIC_INIT(IMU_CAL_GYRO_IDLE);
static struct cal_gyro_acc s_gyro_acc;          /* burst thread only */
static float s_gyro_elapsed;                    /* burst thread only; seconds */
static bool s_gyro_active;                      /* burst thread only */

static bool s_mag_active;                       /* app_wq only */
static bool s_mag_fitted;                       /* app_wq only */
static float s_mag_min[3], s_mag_max[3];        /* app_wq only */

/* helpers: trust the caller */

static void gyro_run_restart(void)
{
	memset(&s_gyro_acc, 0, sizeof(s_gyro_acc));
	s_gyro_elapsed = 0.0f;
}

/* Called by the settings subsystem during load, on the loading thread. name
 * is what follows "imu_tag/cal/". A record of another length or version is
 * ignored and the defaults stay: returning an error would fail the load. */
static int cal_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	if (settings_name_steq(name, "gyro", NULL) != 0) {
		struct cal_gyro_rec rec;

		if (len != sizeof(rec) ||
		    read_cb(cb_arg, &rec, sizeof(rec)) != (ssize_t)sizeof(rec) ||
		    rec.version != CAL_VERSION) {
			LOG_WRN("gyro record ignored, length %u", (unsigned int)len);
			return 0;
		}
		k_spinlock_key_t key = k_spin_lock(&s_cal_lock);

		memcpy(s_gyro_bias, rec.bias, sizeof(s_gyro_bias));
		k_spin_unlock(&s_cal_lock, key);
	} else if (settings_name_steq(name, "mag", NULL) != 0) {
		struct cal_mag_rec rec;

		if (len != sizeof(rec) ||
		    read_cb(cb_arg, &rec, sizeof(rec)) != (ssize_t)sizeof(rec) ||
		    rec.version != CAL_VERSION) {
			LOG_WRN("mag record ignored, length %u", (unsigned int)len);
			return 0;
		}
		k_spinlock_key_t key = k_spin_lock(&s_cal_lock);

		memcpy(s_mag_bias, rec.bias, sizeof(s_mag_bias));
		memcpy(s_mag_scale, rec.scale, sizeof(s_mag_scale));
		k_spin_unlock(&s_cal_lock, key);
	}
	return 0;
}

/* Static: placed by the linker, so it exists before any settings_subsys_init()
 * and has no registration order to get wrong. */
SETTINGS_STATIC_HANDLER_DEFINE(imu_cal, "imu_tag/cal", NULL, cal_set, NULL, NULL);

/* public API */

void imu_cal_apply_gyro(imu_vec3_t *g)
{
	float bias[3];
	k_spinlock_key_t key = k_spin_lock(&s_cal_lock);

	memcpy(bias, s_gyro_bias, sizeof(bias));
	k_spin_unlock(&s_cal_lock, key);

	g->x -= bias[0];
	g->y -= bias[1];
	g->z -= bias[2];
}

void imu_cal_apply_mag(imu_vec3_t *m)
{
	float bias[3], scale[3];
	k_spinlock_key_t key = k_spin_lock(&s_cal_lock);

	memcpy(bias, s_mag_bias, sizeof(bias));
	memcpy(scale, s_mag_scale, sizeof(scale));
	k_spin_unlock(&s_cal_lock, key);

	m->x = (m->x - bias[0]) * scale[0];
	m->y = (m->y - bias[1]) * scale[1];
	m->z = (m->z - bias[2]) * scale[2];
}

void imu_cal_get_values(struct imu_cal_values *out)
{
	k_spinlock_key_t key = k_spin_lock(&s_cal_lock);

	memcpy(out->gyro_bias, s_gyro_bias, sizeof(out->gyro_bias));
	memcpy(out->mag_bias, s_mag_bias, sizeof(out->mag_bias));
	memcpy(out->mag_scale, s_mag_scale, sizeof(out->mag_scale));
	k_spin_unlock(&s_cal_lock, key);
}

void imu_cal_gyro_start(void)
{
	atomic_set(&s_gyro_req, 1);
}

void imu_cal_gyro_feed(const imu_vec3_t *raw, float dt, bool discontinuity)
{
	const float g[3] = {raw->x, raw->y, raw->z};
	float bias[3], spread;

	if (atomic_set(&s_gyro_req, 0) != 0) { /* returns the old value */
		gyro_run_restart();
		s_gyro_active = true;
		atomic_set(&s_gyro_state, IMU_CAL_GYRO_RUNNING);
	}
	if (!s_gyro_active) {
		return;
	}
	if (discontinuity) {
		/* samples are missing: the two seconds start again */
		gyro_run_restart();
	}

	cal_gyro_add(&s_gyro_acc, g);
	s_gyro_elapsed += dt;
	if (s_gyro_elapsed < CAL_GYRO_RUN_S) {
		return;
	}

	s_gyro_active = false;
	if (cal_gyro_finish(&s_gyro_acc, bias, &spread) != 0 || spread > CAL_GYRO_MAX_STD) {
		/* a bias taken while moving has no other symptom: keep the old one */
		atomic_set(&s_gyro_state, IMU_CAL_GYRO_MOVED);
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&s_cal_lock);

	memcpy(s_gyro_bias, bias, sizeof(s_gyro_bias));
	k_spin_unlock(&s_cal_lock, key);
	atomic_set(&s_gyro_state, IMU_CAL_GYRO_DONE);
}

enum imu_cal_gyro_state imu_cal_gyro_state(void)
{
	return (enum imu_cal_gyro_state)atomic_get(&s_gyro_state);
}

int imu_cal_gyro_save(void)
{
	struct cal_gyro_rec rec = {.version = CAL_VERSION};

	if (imu_cal_gyro_state() != IMU_CAL_GYRO_DONE) {
		return -EAGAIN;
	}
	k_spinlock_key_t key = k_spin_lock(&s_cal_lock);

	memcpy(rec.bias, s_gyro_bias, sizeof(rec.bias));
	k_spin_unlock(&s_cal_lock, key);

	return settings_save_one("imu_tag/cal/gyro", &rec, sizeof(rec));
}

void imu_cal_mag_start(void)
{
	for (int i = 0; i < 3; i++) {
		s_mag_min[i] = FLT_MAX;
		s_mag_max[i] = -FLT_MAX;
	}
	s_mag_active = true;
}

void imu_cal_mag_feed(const imu_vec3_t *raw)
{
	const float m[3] = {raw->x, raw->y, raw->z};

	if (!s_mag_active) {
		return;
	}
	for (int i = 0; i < 3; i++) {
		s_mag_min[i] = (m[i] < s_mag_min[i]) ? m[i] : s_mag_min[i];
		s_mag_max[i] = (m[i] > s_mag_max[i]) ? m[i] : s_mag_max[i];
	}
}

int imu_cal_mag_finish(void)
{
	float bias[3], scale[3];
	int rc;

	if (!s_mag_active) {
		return -EALREADY;
	}
	s_mag_active = false;

	rc = cal_mag_fit(s_mag_min, s_mag_max, CAL_MAG_MIN_RADIUS, bias, scale);
	if (rc != 0) {
		return rc;
	}

	k_spinlock_key_t key = k_spin_lock(&s_cal_lock);

	memcpy(s_mag_bias, bias, sizeof(s_mag_bias));
	memcpy(s_mag_scale, scale, sizeof(s_mag_scale));
	k_spin_unlock(&s_cal_lock, key);
	s_mag_fitted = true;
	return 0;
}

int imu_cal_mag_save(void)
{
	struct cal_mag_rec rec = {.version = CAL_VERSION};

	if (!s_mag_fitted) {
		return -EAGAIN;
	}
	k_spinlock_key_t key = k_spin_lock(&s_cal_lock);

	memcpy(rec.bias, s_mag_bias, sizeof(rec.bias));
	memcpy(rec.scale, s_mag_scale, sizeof(rec.scale));
	k_spin_unlock(&s_cal_lock, key);

	return settings_save_one("imu_tag/cal/mag", &rec, sizeof(rec));
}
