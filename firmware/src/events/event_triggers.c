/* event_triggers.c - INT2 hardware events. Owns: s_evt_work, s_isr_count.
 * Runs on: the INT2 ISR (notify only) and the system work queue (handler,
 * arm, disarm). Register access goes through imu_ext under s_imu_lock. */
#include "events/event_triggers.h"
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>

LOG_MODULE_REGISTER(event_triggers, LOG_LEVEL_INF); /* Later: CONFIG_IMU_TAG_LOG_LEVEL, chapter 3 */

/* static state */
static void evt_work_handler(struct k_work *work);
static K_WORK_DEFINE(s_evt_work, evt_work_handler);
static atomic_t s_isr_count = ATOMIC_INIT(0);

/* helpers: trust the caller */

/* Runs on the system work queue; takes s_imu_lock through imu_ext. Stub. */
static void evt_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	LOG_DBG("event work: not implemented");
}

/* public API: validate at the boundary */

int event_triggers_init(void)
{
	return -ENOSYS;
}

int event_triggers_arm(uint8_t mask)
{
	ARG_UNUSED(mask);
	return -ENOSYS;
}

int event_triggers_disarm(void)
{
	return -ENOSYS;
}

void event_triggers_notify(void)
{
	atomic_inc(&s_isr_count);
	k_work_submit(&s_evt_work);
}

uint32_t event_triggers_isr_count(void)
{
	return (uint32_t)atomic_get(&s_isr_count);
}
