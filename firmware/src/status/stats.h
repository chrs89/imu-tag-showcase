#ifndef APP_STATUS_STATS_H
#define APP_STATUS_STATS_H

#include <zephyr/kernel.h>

struct periodic_log {
        struct k_work_delayable dwork;
        uint32_t period_ms;
};

/** Called from main at boot, after app_wq_init(). Schedules the 5 s
 *  counter log on app_wq. */
void stats_init(void);

#endif // APP_STATUS_STATS_H