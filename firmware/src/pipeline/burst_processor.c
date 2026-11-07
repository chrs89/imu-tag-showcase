/* Burst processor: drains the IMU FIFO once per watermark interrupt.
 * Transfer step 2: thread skeleton only. Later: chapter 4 wires INT2 to
 * burst_processor_signal().
 */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "pipeline/burst_processor.h"

LOG_MODULE_REGISTER(imu_burst_proc, LOG_LEVEL_INF);

#define BURST_PROCESSOR_STACK    1024 /* measured in task 3, resized in task 4 */
#define BURST_PROCESSOR_PRIORITY 6    /* chapter 3 table, preemptible */

static K_THREAD_STACK_DEFINE(s_bp_stack, BURST_PROCESSOR_STACK); /* thread_stack.h:679 */
static struct k_thread s_bp_thread;
static K_SEM_DEFINE(s_bp_sem, 0, 1);                              /* kernel.h:3729 */
static atomic_t s_bursts = ATOMIC_INIT(0);                        /* sys/atomic.h:61 */

static void burst_processor_thread_fn(void *p1, void *p2, void *p3) /* arch_interface.h:48 */
{
        ARG_UNUSED(p1);
        ARG_UNUSED(p2);
        ARG_UNUSED(p3);

        LOG_INF("burst_proc prio %d, stack %zu",
                k_thread_priority_get(k_current_get()),                   /* kernel.h:1060, 836 */
                K_THREAD_STACK_SIZEOF(s_bp_stack));                       /* thread_stack.h:599 */

        while (1) {
                k_sem_take(&s_bp_sem, K_FOREVER);                         /* kernel.h:3674 */
                atomic_inc(&s_bursts);                                    /* sys/atomic.h:315 */
        }
}

void burst_processor_signal(void)
{
        k_sem_give(&s_bp_sem);                                        /* kernel.h:3686 */
}

uint32_t burst_processor_count(void)
{
        return (uint32_t)atomic_get(&s_bursts);
}

int burst_processor_init(void)
{
        k_thread_create(&s_bp_thread, s_bp_stack, K_THREAD_STACK_SIZEOF(s_bp_stack),
                        burst_processor_thread_fn, NULL, NULL, NULL,
                        BURST_PROCESSOR_PRIORITY, 0, K_NO_WAIT);              /* kernel.h:444 */
        k_thread_name_set(&s_bp_thread, "burst_proc");                /* kernel.h:1499 */

        return 0;
}
