#ifndef PIPELINE_BURST_PROCESSOR_H
#define PIPELINE_BURST_PROCESSOR_H

#include <stdint.h>

int burst_processor_init(void);
void burst_processor_signal(void);
uint32_t burst_processor_count(void);

#endif