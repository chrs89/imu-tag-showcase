#ifndef APP_APP_WQ_H
#define APP_APP_WQ_H

#include <zephyr/kernel.h>

/** Called from main at boot, before any module that submits to it.
 *  Starts the preemptible application work queue (priority 8, 2048 B). */
void app_wq_init(void);

/** Called from any thread. @return the application work queue for
 *  k_work_submit_to_queue() and k_work_schedule_for_queue(). */
struct k_work_q *app_wq_get(void);

#endif