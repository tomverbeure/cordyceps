#ifndef BDM_H
#define BDM_H

#include <stdbool.h>
#include <stdint.h>

#define BDM_STATUS_TIMEOUT 0xff

void bdm_init();
bool bdm_hreset_and_halt_cpu(void);
bool bdm_halt_cpu(void);
bool bdm_restore_boot_clock(void);
bool bdm_request_resume(void);
bool bdm_resume_cpu();

bool bdm_set_watchpoint(uint32_t address);
bool bdm_clear_watchpoint(void);
bool bdm_watchpoint_hit(uint32_t address);
bool bdm_wait_for_watchpoint(uint32_t address, uint32_t timeout_ms);

bool bdm_instruction(uint32_t instruction);
bool bdm_read_gpr(unsigned reg, uint32_t *value);
bool bdm_read_word(uint32_t address, uint32_t *value);
bool bdm_read_halfword(uint32_t address, uint16_t *value);

bool bdm_wait_until_freeze(uint32_t timeout_us);
bool bdm_wait_until_ready(uint32_t timeout_us);
bool bdm_xfer(bool mode, bool cntrl, uint32_t tx_data, uint8_t *s, uint32_t *rx_data, uint32_t timeout_us);

#endif
