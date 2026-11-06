/* imu_fifo.c - INT1 watermark path. Owns: s_fifo_sem, s_burst_count, the
 * INT1 GPIO callback. Runs on: the INT1 ISR (notify only) and the burst
 * thread (wait, burst, re-prime). Register access goes through imu_ext. */
#include "imu/imu_fifo.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>

LOG_MODULE_REGISTER(imu_fifo, LOG_LEVEL_INF); /* Later: CONFIG_IMU_TAG_LOG_LEVEL, chapter 3 */

/* static state */
static K_SEM_DEFINE(s_fifo_sem, 0, 1);
static atomic_t s_burst_count = ATOMIC_INIT(0);

/* helpers: trust the caller */

/* public API: validate at the boundary */

int imu_fifo_init(uint16_t watermark_rows)
{
	ARG_UNUSED(watermark_rows);
	return -ENOSYS;
}

void imu_fifo_notify(void)
{
	k_sem_give(&s_fifo_sem);
}

void imu_fifo_wait(void)
{
	k_sem_take(&s_fifo_sem, K_FOREVER);
}

void imu_fifo_do_burst(void)
{
	LOG_DBG("burst: not implemented");
}

bool imu_fifo_int1_active(void)
{
	return false;
}

uint32_t imu_fifo_burst_count(void)
{
	return (uint32_t)atomic_get(&s_burst_count);
}
