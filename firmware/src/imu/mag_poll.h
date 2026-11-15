/*
 * mag_poll.h - LIS2MDL magnetometer poll, 10 samples per second.
 * Context: a delayable work item on app_wq reads the sensor through the
 * Zephyr sensor API (I2C, sleeps) and publishes the calibrated sample under
 * a spinlock; any thread takes a copy. Units: uT, microseconds of MCU uptime.
 */
#ifndef IMU_MAG_POLL_H_
#define IMU_MAG_POLL_H_
#include <stdint.h>
#include "imu/imu_types.h"

/** Counters of the poll; a copy, each field read atomically. */
struct mag_poll_stats {
	uint32_t polls;  /* polls that returned a sample */
	uint32_t errors; /* polls the sensor API refused */
	uint32_t stuck;  /* polls published as invalid: 20 identical readings in a row */
};

/** Called from main at boot, after app_wq_init(); schedules the first poll.
 *  @return 0, or -ENODEV when the LIS2MDL driver is not ready. */
int mag_poll_init(void);

/** Called from any thread; copies five words with interrupts masked.
 *  out->valid is false until the first poll and while the sensor is stuck. */
void mag_poll_get(imu_mag_sample_t *out);

/** Called from any thread; three atomic reads. */
void mag_poll_get_stats(struct mag_poll_stats *out);

#endif /* IMU_MAG_POLL_H_ */
