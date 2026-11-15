/*
 * imu_calibration.h - gyro bias and magnetometer hard and soft iron values,
 * the two procedures that find them, and their records in the settings
 * subsystem ("imu_tag/cal/gyro", "imu_tag/cal/mag").
 * Context: the apply functions run on the burst thread and on app_wq and copy
 * the values under a spinlock. The gyro run is fed on the burst thread, the
 * magnetometer run on app_wq; start, finish and save are called from app_wq
 * only, because a save is a flash write. Units: rad/s, uT.
 */
#ifndef IMU_CALIBRATION_H_
#define IMU_CALIBRATION_H_
#include <stdbool.h>
#include "imu/imu_types.h"

enum imu_cal_gyro_state {
	IMU_CAL_GYRO_IDLE,    /* no run since boot */
	IMU_CAL_GYRO_RUNNING, /* collecting two seconds of samples */
	IMU_CAL_GYRO_DONE,    /* the new bias is applied and can be saved */
	IMU_CAL_GYRO_MOVED,   /* spread above 0.5 dps: the tag was moved, bias unchanged */
};

/** The values in force; a copy taken under the lock. */
struct imu_cal_values {
	float gyro_bias[3]; /* rad/s */
	float mag_bias[3];  /* uT */
	float mag_scale[3]; /* 1.0 when uncalibrated */
};

/** Called from the burst thread or app_wq; g = g - bias, in place. */
void imu_cal_apply_gyro(imu_vec3_t *g);

/** Called from app_wq or the burst thread; m = (m - bias) * scale, in place. */
void imu_cal_apply_mag(imu_vec3_t *m);

/** Called from any thread; copies nine floats under the lock. */
void imu_cal_get_values(struct imu_cal_values *out);

/** Called from app_wq; requests a gyro bias run. The tag must be at rest.
 *  The burst thread starts the run at its next sample. */
void imu_cal_gyro_start(void);

/** Called from the burst thread for every sample with the raw gyro value.
 *  Collects while a run is active; a discontinuity restarts the run. After
 *  two seconds of samples the bias is applied unless the tag was moved. */
void imu_cal_gyro_feed(const imu_vec3_t *raw, float dt, bool discontinuity);

/** Called from any thread; one atomic read. */
enum imu_cal_gyro_state imu_cal_gyro_state(void);

/** Called from app_wq only; writes the gyro record to flash.
 *  @return 0, -EAGAIN when no run has finished with the tag at rest, or the
 *  error of settings_save_one(). */
int imu_cal_gyro_save(void);

/** Called from app_wq only; starts collecting magnetometer extremes. The
 *  tag is then turned through all orientations. */
void imu_cal_mag_start(void);

/** Called from app_wq only (the magnetometer poll) with the raw value in uT. */
void imu_cal_mag_feed(const imu_vec3_t *raw);

/** Called from app_wq only; ends the run and applies the fitted values.
 *  @return 0, -EALREADY when no run is active, -ENODATA when an axis covered
 *  less than 10 uT of radius (values unchanged). */
int imu_cal_mag_finish(void);

/** Called from app_wq only; writes the magnetometer record to flash.
 *  @return 0, -EAGAIN when no run has finished since boot, or the error of
 *  settings_save_one(). */
int imu_cal_mag_save(void);

#endif /* IMU_CALIBRATION_H_ */
