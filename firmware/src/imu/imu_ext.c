/* imu_ext.c - the only owner of the LSM6DSV16X registers. Owns: s_imu_lock,
 * the ST HAL context (chapter 8). Runs on: boot, the burst thread and the
 * system work queue; every register access happens under s_imu_lock. */
#include "imu/imu_ext.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

LOG_MODULE_REGISTER(imu_ext, LOG_LEVEL_INF); /* Later: CONFIG_IMU_TAG_LOG_LEVEL, chapter 3 */

/* static state */
static K_MUTEX_DEFINE(s_imu_lock);

/* helpers: trust the caller (hold s_imu_lock) */

/* public API: validate at the boundary */

int imu_ext_init(void)
{
	return -ENOSYS;
}

int imu_ext_set_odr(uint16_t odr_hz)
{
	ARG_UNUSED(odr_hz);
	return -ENOSYS;
}

int imu_ext_set_accel_range(uint8_t range_g)
{
	ARG_UNUSED(range_g);
	return -ENOSYS;
}

int imu_ext_fifo_read(uint8_t *buf, uint16_t max_rows)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(max_rows);
	return -ENOSYS;
}

int imu_ext_read_event_status(uint8_t *status)
{
	ARG_UNUSED(status);
	return -ENOSYS;
}
