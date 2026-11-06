/*
 * event_triggers.h - INT2 hardware events: free-fall, tap, motion.
 * Context: the INT2 ISR submits s_evt_work; the handler runs on the system
 * work queue and reads the status through imu_ext. Arming is a work-queue
 * command. See docs/adr/ADR-003.
 */
#ifndef EVENT_TRIGGERS_H_
#define EVENT_TRIGGERS_H_
#include <stdint.h>
#include <stdbool.h>

#define EVT_FREE_FALL (1u << 0)
#define EVT_TAP       (1u << 1)
#define EVT_MOTION    (1u << 2)

/** Called from main at boot, after imu_ext_init(); registers the INT2 GPIO
 *  callback that submits s_evt_work.
 *  @return 0, -ENODEV if the INT2 GPIO is not ready. */
int event_triggers_init(void);

/** Called from the system work queue; takes s_imu_lock through imu_ext.
 *  Programs the event engine for the EVT_* bits in @p mask.
 *  @return 0, -EINVAL for an empty or unknown mask, -EBUSY unless the ODR
 *  is 240 Hz (ADR-003), -EIO on a bus error. */
int event_triggers_arm(uint8_t mask);

/** Called from the system work queue; takes s_imu_lock through imu_ext.
 *  @return 0, -EIO on a bus error. */
int event_triggers_disarm(void);

/** Called from the INT2 ISR, or from a test in place of it; atomic_inc on
 *  s_isr_count and k_work_submit of s_evt_work, nothing else. */
void event_triggers_notify(void);

/** Called from any thread; atomic_get on s_isr_count.
 *  @return INT2 interrupts seen since boot. */
uint32_t event_triggers_isr_count(void);

#endif /* EVENT_TRIGGERS_H_ */
