/*
 * imu_types.h - the sample every sink receives.
 * No Zephyr include: this header is shared with the host tests.
 * Units: accel in m/s2, gyro in rad/s, time in microseconds of MCU uptime.
 */
#ifndef IMU_TYPES_H_
#define IMU_TYPES_H_
#include <stdbool.h>
#include <stdint.h>

typedef struct {
	float x, y, z;
} imu_vec3_t;

/* The latest magnetometer sample, as the poll publishes it. */
typedef struct {
	imu_vec3_t mag;        /* uT, calibrated */
	uint32_t timestamp_us; /* MCU uptime at the poll; same clock as imu_sample_t */
	bool valid;            /* false until the first poll and while the sensor is stuck */
} imu_mag_sample_t;

#define ORIENT_VALID        (1u << 0)
#define ORIENT_USED_MAG     (1u << 1) /* this step was a 9-axis step */
#define ORIENT_MAG_REJECTED (1u << 2) /* 9-axis wanted; sample missing, old or out of range */

/* The published orientation: earth frame x north, y west, z up. */
typedef struct {
	float w, x, y, z;      /* unit quaternion, body to earth */
	uint32_t timestamp_us; /* of the sample it was computed from */
	uint8_t seq;
	uint8_t flags;         /* ORIENT_* */
} imu_orientation_t;

/* The sample is not the successor of the previous one: FIFO overrun,
 * clipped burst, or a FIFO flush for a rate change. */
#define IMU_SAMPLE_DISCONTINUITY (1u << 0)

typedef struct {
	imu_vec3_t accel;      /* m/s2 */
	imu_vec3_t gyro;       /* rad/s */
	uint32_t timestamp_us; /* MCU uptime; wraps every 71.6 min, compare by unsigned difference */
	uint8_t seq;           /* rolls over at 256 */
	uint8_t flags;         /* IMU_SAMPLE_* */
} imu_sample_t;

#endif /* IMU_TYPES_H_ */
