#include "app/app_wq.h"

#define APP_WQ_STACK 2048                       /* measured in task 3 */
#define APP_WQ_PRIO  K_PRIO_PREEMPT(8)          /* kernel.h 55; A3 ladder */

static K_THREAD_STACK_DEFINE(s_app_wq_stack, APP_WQ_STACK);   /* thread_stack.h 679 */
static struct k_work_q s_app_wq;                               /* kernel.h 4652 */

void app_wq_init(void)
{
        const struct k_work_queue_config cfg = { .name = "app_wq" };   /* kernel.h 4614-4649 */

        k_work_queue_init(&s_app_wq);                                  /* kernel.h 4029 */
        k_work_queue_start(&s_app_wq, s_app_wq_stack,
                           K_THREAD_STACK_SIZEOF(s_app_wq_stack), APP_WQ_PRIO, &cfg);
}

struct k_work_q *app_wq_get(void) { return &s_app_wq; }


