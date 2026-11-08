#ifndef PIPELINE_BURST_PROCESSOR_H
#define PIPELINE_BURST_PROCESSOR_H

#include <stdint.h>

/** Called to initialize the burst processor. 
 *  @return 0 on success, negative error code on failure.
*/
int burst_processor_init(void);

/** Called from any thread; atomic_get on s_bursts.
 *  @return bursts completed since boot. */
uint32_t burst_processor_count(void);

#endif