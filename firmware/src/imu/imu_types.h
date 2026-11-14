/*
 * imu_types.h - the sample every sink receives.
 * No Zephyr include: this header is shared with the host tests.
 * Units: accel in m/s2, gyro in rad/s, time in microseconds of MCU uptime.
 */
#ifndef IMU_TYPES_H_
#define IMU_TYPES_H_
#include <stdint.h>

typedef struct {
	float x, y, z;
} imu_vec3_t;

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
