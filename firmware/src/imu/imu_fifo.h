/*
 * imu_fifo.h - INT1 watermark path: ISR signal, burst read, decode hand-off.
 * Context: the INT1 ISR gives s_fifo_sem; the burst thread waits on it and
 * reads the FIFO through imu_ext. Units: watermark in FIFO rows.
 * See docs/adr/ADR-001.
 */
#ifndef IMU_FIFO_H_
#define IMU_FIFO_H_
#include <stdint.h>
#include <stdbool.h>

/** Called from main at boot, after imu_ext_init(); registers the INT1 GPIO
 *  callback that gives s_fifo_sem and configures the watermark.
 *  @return 0, -ENODEV if the INT1 GPIO is not ready, -EIO on a bus error. */
int imu_fifo_init(uint16_t watermark_rows);

/** Called from the INT1 ISR, or from a test in place of it; only gives
 *  s_fifo_sem. No bus access, no logging. */
void imu_fifo_notify(void);

/** Called from the burst thread; blocks on s_fifo_sem with K_FOREVER until
 *  the ISR or the re-prime gives it. */
void imu_fifo_wait(void);

/** Called from the burst thread; takes s_imu_lock through imu_ext for one
 *  SPI transaction, decodes the rows and runs the registered sinks. Gives
 *  s_fifo_sem again if INT1 is still active (re-prime). */
void imu_fifo_do_burst(void);

/** Called from the burst thread; reads the INT1 pin, touches no kernel object.
 *  @return true while the FIFO still holds at least the watermark. */
bool imu_fifo_int1_active(void);

/** Called from any thread; atomic_get on s_burst_count.
 *  @return bursts completed since boot. */
uint32_t imu_fifo_burst_count(void);

#endif /* IMU_FIFO_H_ */
