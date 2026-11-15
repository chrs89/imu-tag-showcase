/* mag_poll.c - magnetometer poll. Owns: the LIS2MDL device handle, the 100 ms
 * work item, the latest sample under s_mag_lock, the poll counters. Runs on:
 * app_wq (the handler; the I2C transfer sleeps) and any reader (the getters).
 * Never touches the LSM6DSV16X. */
#include "imu/mag_poll.h"
#include <errno.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>
#include "app/app_wq.h"
#include "imu/imu_calibration.h"

#define MAG_POLL_MS     100    /* the driver sets the sensor to 10 Hz, continuous mode */
#define MAG_STUCK_POLLS 20     /* identical readings in a row: 2 s; a choice, unverified */
#define GAUSS_TO_UT     100.0f /* SENSOR_CHAN_MAGN_XYZ is in Gauss */

/* static state */
static const struct device *const s_mag = DEVICE_DT_GET(DT_ALIAS(mag_sensor0));
static struct k_spinlock s_mag_lock;
static imu_mag_sample_t s_latest;     /* under s_mag_lock: five words read as one set */
static struct sensor_value s_prev[3]; /* app_wq only */
static uint8_t s_same;                /* app_wq only */
static atomic_t s_polls, s_errors, s_stuck;

static void mag_poll_fn(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(s_mag_work, mag_poll_fn);

/* Runs on app_wq every 100 ms. The driver's fetch returns 0 even after a
 * failed I2C transfer and keeps the old counts, so a constant reading is the
 * only sign of a bus fault. */
static void mag_poll_fn(struct k_work *work)
{
	struct sensor_value v[3];
	imu_mag_sample_t m = {.valid = true};
	int rc;

	ARG_UNUSED(work);
	/* first: the period is then 100 ms from start to start */
	(void)k_work_schedule_for_queue(app_wq_get(), &s_mag_work, K_MSEC(MAG_POLL_MS));

	/* the channel form skips the die temperature the all-channels fetch reads */
	rc = sensor_sample_fetch_chan(s_mag, SENSOR_CHAN_MAGN_XYZ);
	if (rc == 0) {
		rc = sensor_channel_get(s_mag, SENSOR_CHAN_MAGN_XYZ, v);
	}
	if (rc != 0) {
		atomic_inc(&s_errors);
		return;
	}
	atomic_inc(&s_polls);

	s_same = (memcmp(v, s_prev, sizeof(v)) == 0) ? MIN(s_same + 1, MAG_STUCK_POLLS) : 0;
	memcpy(s_prev, v, sizeof(v));
	if (s_same >= MAG_STUCK_POLLS) {
		atomic_inc(&s_stuck);
		m.valid = false;
	}

	/* Axes as the driver reports them; their alignment with the IMU axes
	 * is unverified until the bench check of wiki chapter 10 task 8. */
	m.mag.x = sensor_value_to_float(&v[0]) * GAUSS_TO_UT;
	m.mag.y = sensor_value_to_float(&v[1]) * GAUSS_TO_UT;
	m.mag.z = sensor_value_to_float(&v[2]) * GAUSS_TO_UT;
	imu_cal_mag_feed(&m.mag); /* the collector sees raw values */
	imu_cal_apply_mag(&m.mag);
	m.timestamp_us = (uint32_t)k_ticks_to_us_floor64((uint64_t)k_uptime_ticks());

	k_spinlock_key_t key = k_spin_lock(&s_mag_lock);

	s_latest = m;
	k_spin_unlock(&s_mag_lock, key);
}

/* public API */

int mag_poll_init(void)
{
	if (!device_is_ready(s_mag)) {
		return -ENODEV;
	}
	(void)k_work_schedule_for_queue(app_wq_get(), &s_mag_work, K_NO_WAIT);
	return 0;
}

void mag_poll_get(imu_mag_sample_t *out)
{
	k_spinlock_key_t key = k_spin_lock(&s_mag_lock);

	*out = s_latest;
	k_spin_unlock(&s_mag_lock, key);
}

void mag_poll_get_stats(struct mag_poll_stats *out)
{
	out->polls = (uint32_t)atomic_get(&s_polls);
	out->errors = (uint32_t)atomic_get(&s_errors);
	out->stuck = (uint32_t)atomic_get(&s_stuck);
}
