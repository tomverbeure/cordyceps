#include <stdio.h>

#include "pico/stdlib.h"
#include "tusb.h"

#include "bdm.h"
#include "pins.h"

#include "vxw_uart.h"

// Needs to be very long at cold power up. Not sure why...
#define BDM_FRZ_TIMEOUT_US  1000000

// 15 NOPs = 15 cycles (~100 ns at 150 MHz)
#define NOP15() __asm volatile ( \
    "nop\n\t nop\n\t nop\n\t nop\n\t nop\n\t" \
    "nop\n\t nop\n\t nop\n\t nop\n\t nop\n\t" \
    "nop\n\t nop\n\t nop\n\t nop\n\t nop\n\t" \
)

// Wait time for 1/2 clock period -> ~5 MHz BDM clock
static inline void wait_100ns(void) {
    NOP15();
}

// When true, the CPU was send immeidately into debug mode after coming
// out of reset
static bool halted_at_reset;

void bdm_init()
{
    halted_at_reset = false;

    gpio_init(BDM_HRESET);
    gpio_put(BDM_HRESET, 0);
    gpio_set_dir(BDM_HRESET, GPIO_OUT);

    gpio_init(BDM_FRZ);
    gpio_set_dir(BDM_FRZ, GPIO_IN);

    gpio_init(BDM_DSCK);
    gpio_set_dir(BDM_DSCK, GPIO_OUT);

    gpio_init(BDM_DSDI);
    gpio_set_dir(BDM_DSDI, GPIO_OUT);

    gpio_init(BDM_DSDO);
    gpio_set_dir(BDM_DSDO, GPIO_IN);

    gpio_init(SD_SEL);
    gpio_set_dir(SD_SEL, GPIO_OUT);
    gpio_put(SD_SEL, 0);

    gpio_init(nBDM_SD_ENA);
    gpio_set_dir(nBDM_SD_ENA, GPIO_OUT);
    gpio_put(nBDM_SD_ENA, 0);
}

bool bdm_hreset_and_halt_cpu(void)
{
    halted_at_reset = false;

    // BDM_HRESET is active low on the CPU but the PCB has an inverting MOSFET 
    // that also makes it open-drain (because the CPU itself can drive HRESET.)
    gpio_put(BDM_HRESET, 1);

    // Enable debug mode: section 44.3.1.1
    // DSCK must be high during reset.
    // The state of DSCK is sampled 3 CLKOUT clock cycles before the negation of reset

    // Selection of clock mode: section 44.3.2.3.3
    // DSDI is sampled 8 clocks after negation of reset. If low, then
    // asynchronous mode is enabled.

    // Enable debug port
    gpio_put(BDM_DSCK, 1);
    // DSDI selects the BDM clocking mode. 0 -> asynchronous mode: the RP2350 
    // supplies DSCK and times DSDI/DSDO signals off that instead of CLKOUT.
    gpio_put(BDM_DSDI, 0);

    sleep_ms(1);
    gpio_put(BDM_HRESET, 0);

    bool ret = bdm_wait_until_freeze(BDM_FRZ_TIMEOUT_US);
    if (ret == false){
        // CPU didn't enter debug mode. This shouldn't happen.
        return false;
    }

    // The CPU is now in debug mode!
    halted_at_reset = true;
    ret = bdm_restore_boot_clock();

    return ret;
}

// Table 44-12 / Section 44.3.2.4.2
enum {
    BDM_STATUS_DATA           = 0,      // Valid data returned by the CPU
    BDM_STATUS_SEQUENCE_ERROR = 1,      // Instruction/data transfer mismatch
    BDM_STATUS_CORE_INTERRUPT = 2,      // Exception during debug execution
    BDM_STATUS_NULL           = 3,      // No data returned
};


enum {
    BDM_MODE_INSTRUCTION_DATA = 0,
    BDM_MODE_TRAP_COMMAND     = 1,
};

enum {
    // With BDM_MODE_INSTRUCTION_DATA:
    BDM_CNTRL_INSTRUCTION = 0,
    BDM_CNTRL_DATA        = 1,

    // With BDM_MODE_TRAP_COMMAND:
    BDM_CNTRL_TRAP_ENABLE = 0,
    BDM_CNTRL_COMMAND     = 1,
};

enum {
    BDM_CMD_ASSERT_NONMASKABLE_BREAK = 0x5F,
    BDM_CMD_CLEAR_BREAK_REQUESTS     = 0x1F,
};


// MPC860 special-purpose registers
enum {
    SPR_SRR0        = 26,               // Save/Restore Register 0: return instruction address for RFI
    SPR_SRR1        = 27,               // Save/Restore Register 1: saved machine state restored by RFI

    // CMPA-CMPD: instruction address execution breakpoints
    SPR_CMPA        = 144,              // Comparator A: instruction-address comparison for breakpoints
    SPR_ICR         = 148,              // Interrupt Cause Register
    SPR_DER         = 149,              // Debug Enable Register

    // CMPE-CMPF: memory read/write watchpoints
    SPR_CMPE        = 152,              // Data address comparator E

    // CMPG-CMPH: data-value watchpoints
    SPR_CMPG        = 154,              // Data value comparator G

    SPR_LCTRL1      = 156,              // Load/store comparator configuration
    SPR_LCTRL2      = 157,              // Load/store watchpoint configuration
    SPR_ICTRL       = 158,              // Instruction Support Control Register
    SPR_BAR         = 159,              // Address of the access causing a data breakpoint
    SPR_IC_CST      = 560,              // Instruction Cache Control and Status Register
    SPR_DPDR        = 630,              // Development Port Data Register: CPU/debugger data exchange
};

#define PPC_BIT(n)      (1u << (31 - (n)))
#define PPC_NOP         0x60000000
#define PPC_RFI         0x4c000064

// MFSPR/MTSPR instruction with r0 as the scratch register. 
static uint32_t bdm_spr_instruction(unsigned spr, bool write)
{
    return 0x7c000000 | ((spr & 31) << 16) | ((spr >> 5) << 11) | ((write ? 467 : 339) << 1);
}

// Status describes the previous operation. Error statuses also mean the
// target discarded the word being sent now; do not continue toward RFI.
static bool bdm_checked_xfer(bool cntrl, uint32_t data, uint32_t *value, uint8_t expected_status)
{
    uint8_t     status;
    uint32_t    rx_data = 0;

    int ret = bdm_xfer(BDM_MODE_INSTRUCTION_DATA, cntrl, data, &status, &rx_data, 1000);
    if (!ret)
        return false;

    if (status != expected_status)
        return false;

    if (value)
        *value = rx_data;

    return true;
}

bool bdm_instruction(uint32_t instruction)
{
    int ret = bdm_checked_xfer(BDM_CNTRL_INSTRUCTION, instruction, NULL, BDM_STATUS_NULL);
    return ret;
}

// Send MTSPR DPDR,rN, then a NOP to clock out the register value.
// See mpc8xx_get_gpr() in mpc8xxbdm.c
bool bdm_read_gpr(unsigned reg, uint32_t *value)
{
    if (reg >= 32 || value == NULL || !gpio_get(BDM_FRZ))
        return false;

    uint32_t instruction = bdm_spr_instruction(SPR_DPDR, true) | (reg << 21);
    int ret = bdm_instruction(instruction);
    if (!ret)
        return false;

    ret = bdm_checked_xfer(BDM_CNTRL_INSTRUCTION, PPC_NOP, value, BDM_STATUS_DATA);
    return ret;
}

static bool bdm_read_r0(uint32_t *value)
{
    int ret = bdm_read_gpr(0, value);
    return ret;
}

// Send MFSPR rN,DPDR, then supply the register value as DPDR data.
// See mpc8xx_set_gpr() in mpc8xxbdm.c
static bool bdm_write_gpr(unsigned reg, uint32_t value)
{
    if (reg >= 32 || !gpio_get(BDM_FRZ))
        return false;

    int ret = bdm_instruction(bdm_spr_instruction(SPR_DPDR, false) | (reg << 21));
    if (!ret)
        return false;

    ret = bdm_checked_xfer(BDM_CNTRL_DATA, value, NULL, BDM_STATUS_NULL);
    return ret;
}

// Copy an SPR register into R0, then read out R0.
static bool bdm_read_spr(unsigned spr, uint32_t *value)
{
    int ret = bdm_instruction(bdm_spr_instruction(spr, false));
    if (!ret)
        return false;

    ret = bdm_read_r0(value);
    return ret;
}

// Copy a value into R0 then load it into an SPR register
static bool bdm_write_spr(unsigned spr, uint32_t value)
{
    int ret = bdm_write_gpr(0, value);
    if (!ret)
        return false;

    ret = bdm_instruction(bdm_spr_instruction(spr, true));
    return ret;
}

// Restore the scratch register and clock a NOP to check the preceding operation.
static bool bdm_restore_r0(uint32_t value)
{
    int ret = bdm_write_gpr(0, value);
    if (!ret)
        return false;
    ret = bdm_instruction(PPC_NOP);
    return ret;
}

// Read memory through R1 while preserving R1 and R0.
static bool bdm_read_memory(uint32_t address, uint32_t instruction, uint32_t *value)
{
    uint32_t saved_r0;
    uint32_t saved_r1;

    if (!bdm_read_gpr(0, &saved_r0) || !bdm_read_gpr(1, &saved_r1))
        return false;

    // Load R1 with the address
    bool ret = bdm_write_gpr(1, address);
    if (ret){
        // Read data at R1 location
        ret = bdm_instruction(instruction);
    }

    uint32_t result;
    if (ret)
        ret = bdm_read_r0(&result);

    // Restore R1 and R0
    bool ret_restore_r1 = bdm_write_gpr(1, saved_r1);
    bool ret_restore_r0 = bdm_restore_r0(saved_r0);
    if (!ret || !ret_restore_r1 || !ret_restore_r0)
        return false;

    *value = result;
    return true;
}

// Read one aligned 32-bit word.
bool bdm_read_word(uint32_t address, uint32_t *value)
{
    // LWZ R0,0(R1)
    return bdm_read_memory(address, 0x80010000, value);
}

// Read one aligned 16-bit register without accessing adjacent registers.
bool bdm_read_halfword(uint32_t address, uint16_t *value)
{
    uint32_t result;
    // LHZ R0,0(R1)
    if (!bdm_read_memory(address, 0xa0010000, &result))
        return false;

    *value = (uint16_t)result;
    return true;
}

static bool bdm_write_memory(uint32_t address, uint32_t value, bool flush_cache)
{
    uint32_t saved_r0;
    uint32_t saved_r1;

    // Save R0 and R1 for later.
    int ret = bdm_read_gpr(0, &saved_r0);
    if (!ret)
        return false;

    ret = bdm_read_gpr(1, &saved_r1);
    if (!ret)
        return false;

    // Load R1 with address and R0 with data
    ret = bdm_write_gpr(1, address);
    if (!ret)
        goto restore;

    ret = bdm_write_gpr(0, value);
    if (!ret)
        goto restore;

    // Store to memory
    // STW R0,0(R1)
    ret = bdm_instruction(0x90010000);
    if (!ret)
        goto restore;

    if (flush_cache) {
        // Data Cache Block Flush for memory address in R1.
        // DCBF 0,R1
        ret = bdm_instruction(0x7c0008ac);
        if (!ret)
            goto restore;

        // Memory synchronization barrier
        // SYNC
        ret = bdm_instruction(0x7c0004ac);
    } else {
        // Enforce In-order Execution of I/O
        // EIEIO     (the ISA architects must have had fun with this.)
        ret = bdm_instruction(0x7c0006ac);
    }
    if (!ret)
        goto restore;

    ret = bdm_instruction(PPC_NOP);

restore:
    bool ret_restore_r1 = bdm_write_gpr(1, saved_r1);
    bool ret_restore_r0 = bdm_restore_r0(saved_r0);
    return ret && ret_restore_r1 && ret_restore_r0;
}

bool bdm_write_word(uint32_t address, uint32_t value)
{
    return bdm_write_memory(address, value, /* flush_cache */ true);
}

// Disable double-clock mode on TDS3000B model.
// This is needed because HRESET doesn't do it, and it leads to all kind
// of bus timing problems that were a !@#$!@#$ to debug.
bool bdm_restore_boot_clock(void)
{
    vxw_log(">>> bdm_restore_boot_clock():\n\r");

    if (!gpio_get(BDM_FRZ))
        return false;

    const uint32_t SCCR     = 0xff000280;
    const uint32_t PLPRCR   = 0xff000284;

    uint32_t saved_r0;
    uint32_t saved_ictrl;

    int ret = bdm_read_r0(&saved_r0);
    if (!ret)
        return false;

    const char *error = "!!! BDM boot clock ICTRL read failed\n\r";
    ret = bdm_read_spr(SPR_ICTRL, &saved_ictrl);
    if (!ret)
        goto restore_r0;


    uint32_t pll;
    uint32_t clock;

    error = "!!! BDM boot clock read failed\n\r";
    ret = bdm_read_word(PLPRCR, &pll);
    if (!ret)
        goto restore_r0;

    ret = bdm_read_word(SCCR, &clock);
    if (!ret)
        goto restore_r0;

    char str[128];
    snprintf(str, sizeof(str), ">>> Boot clock: PLPRCR=0x%08x SCCR=0x%08x. Restoring single rate.\n\r", pll, clock);
    vxw_log(str);

    // MF changes require CPU serialization to be enabled: ICTRL[ISCT_SER]=000 (table 14-10).
    error = "!!! BDM boot clock serialization failed\n\r";
    ret = bdm_write_spr(SPR_ICTRL, saved_ictrl & ~7u);
    if (!ret)
        goto restore_ictrl;

    ret = bdm_instruction(PPC_NOP);
    if (!ret)
        goto restore_ictrl;

    ret = bdm_instruction(PPC_NOP);
    if (!ret)
        goto restore_ictrl;

    // First undouble the PLL (MF=0 -> x1), then remove the bus divider
    // (EBDF=00 -> divide by 1). This avoids temporarily overclocking the bus.
    error = "!!! BDM boot clock write failed\n\r";

    // We're not only clearing MF but also touching all other bits of the register.
    // That's fine at initial powerup, but it clobbers flags that you might need
    // if you do this at random. (We don't plan to do this...)
    ret = bdm_write_memory(PLPRCR, 0x00005000, false);
    if (!ret)
        goto restore_ictrl;

    // SCCR: System Clock and Reset Control.
    // Set everything to reasonable defaults. Same thing here: we're clobbering
    // a lot of things that might only be OK after initial reset.
    ret = bdm_write_memory(SCCR, 0x21800000, false);
    if (!ret)
        goto restore_ictrl;


    // Check if settings change worked...
    error = "!!! BDM boot clock readback failed\n\r";
    ret = bdm_read_word(PLPRCR, &pll);
    if (!ret)
        goto restore_ictrl;

    ret = bdm_read_word(SCCR, &clock);
    if (!ret)
        goto restore_ictrl;

    // PLL status bits can change; verify MF rather than the entire PLPRCR.
    ret = !(pll & 0xfff00000) && clock == 0x21800000;
    if (!ret)
        goto restore_ictrl;

    snprintf(str, sizeof(str), ">>> Boot clock restored: PLPRCR=0x%08x SCCR=0x%08x; PLL multiplier=1.\n\r", pll, clock);
    vxw_log(str);

restore_ictrl:
    int restored_ictrl = bdm_write_spr(SPR_ICTRL, saved_ictrl);

    if (restored_ictrl)
        restored_ictrl = bdm_instruction(PPC_NOP);

    if (restored_ictrl)
        restored_ictrl = bdm_instruction(PPC_NOP);

    if (!restored_ictrl) {
        if (!ret)
            vxw_log(error);
        error = "!!! BDM boot clock ICTRL restore failed\n\r";
        ret = false;
    }

restore_r0:
    if (!ret)
        vxw_log(error);

    int restored = bdm_restore_r0(saved_r0);
    return ret && restored;
}

// Always use instruction comparator A / breakpoint 0.
bool bdm_set_instruction_breakpoint(uint32_t address)
{
    if (!gpio_get(BDM_FRZ) || (address & 3))
        return false;

    uint32_t saved_r0, ictrl, der;
    int ret = bdm_read_r0(&saved_r0);
    if (!ret)
        return false;

    ret = bdm_write_spr(SPR_CMPA, address);
    if (!ret)
        goto restore_r0;

    ret = bdm_read_spr(SPR_ICTRL, &ictrl);
    if (!ret)
        goto restore_r0;

    // CTA=100: equal; IW0=10: comparator A; SIW0EN: software trap.
    // Clear IFM so this instruction match is not bypassed.
    ictrl &= ~(PPC_BIT(0) | PPC_BIT(1) | PPC_BIT(2) | PPC_BIT(12) |
               PPC_BIT(13) | PPC_BIT(24) | PPC_BIT(28));
    ictrl |= PPC_BIT(0) | PPC_BIT(12) | PPC_BIT(20);
    ret = bdm_write_spr(SPR_ICTRL, ictrl);
    if (!ret)
        goto restore_r0;

    ret = bdm_read_spr(SPR_DER, &der);
    if (!ret)
        goto restore_r0;

    // DER[IBRKE]: enter debug mode on an instruction breakpoint.
    ret = bdm_write_spr(SPR_DER, der | PPC_BIT(29));

restore_r0:
    int restored = bdm_restore_r0(saved_r0);
    return ret && restored;
}

bool bdm_clear_instruction_breakpoint(void)
{
    if (!gpio_get(BDM_FRZ))
        return false;

    uint32_t saved_r0, ictrl, der;
    int ret = bdm_read_r0(&saved_r0);
    if (!ret)
        return false;

    ret = bdm_read_spr(SPR_ICTRL, &ictrl);
    if (!ret)
        goto restore_r0;

    ictrl &= ~(PPC_BIT(0) | PPC_BIT(1) | PPC_BIT(2) | PPC_BIT(12) |
               PPC_BIT(13) | PPC_BIT(20) | PPC_BIT(24) | PPC_BIT(28));
    ret = bdm_write_spr(SPR_ICTRL, ictrl);
    if (!ret)
        goto restore_r0;

    // Leave DER[IBRKE] enabled if another instruction breakpoint is active.
    if (!(ictrl & 0x00000ff0)) {
        ret = bdm_read_spr(SPR_DER, &der);
        if (!ret)
            goto restore_r0;

        ret = bdm_write_spr(SPR_DER, der & ~PPC_BIT(29));
    }

restore_r0:
    int restored = bdm_restore_r0(saved_r0);
    return ret && restored;
}

bool bdm_instruction_breakpoint_hit(uint32_t address)
{
    if (!gpio_get(BDM_FRZ))
        return false;

    uint32_t saved_r0, icr, pc;
    int ret = bdm_read_r0(&saved_r0);
    if (!ret)
        return false;

    ret = bdm_read_spr(SPR_ICR, &icr);
    if (!ret)
        goto restore_r0;

    ret = bdm_read_spr(SPR_SRR0, &pc);
    if (!ret)
        goto restore_r0;

    ret = (icr & PPC_BIT(29)) && pc == address;

restore_r0:
    int restored = bdm_restore_r0(saved_r0);
    return ret && restored;
}

bool bdm_set_watchpoint(uint32_t address)
{
    if (!gpio_get(BDM_FRZ))
        return false;

    uint32_t saved_r0, der;
    int ret = bdm_read_r0(&saved_r0);
    if (!ret)
        return false;

    ret = bdm_write_spr(SPR_LCTRL2, 0);
    if (!ret)
        goto restore_r0;

    // Always use watchpoint 0. Watchpoint 1 is currently unused.
    ret = bdm_write_spr(SPR_CMPE, address);
    if (!ret)
        goto restore_r0;

    ret = bdm_write_spr(SPR_LCTRL1, PPC_BIT(0) | PPC_BIT(12));
    if (!ret)
        goto restore_r0;

    ret = bdm_read_spr(SPR_DER, &der);
    if (!ret)
        goto restore_r0;

    ret = bdm_write_spr(SPR_DER, der | PPC_BIT(28));
    if (!ret)
        goto restore_r0;

    ret = bdm_write_spr(SPR_LCTRL2, PPC_BIT(0) | PPC_BIT(6) | PPC_BIT(30));

restore_r0:
    int restored = bdm_restore_r0(saved_r0);

    return ret && restored;
}

bool bdm_clear_watchpoint(void)
{
    if (!gpio_get(BDM_FRZ))
        return false;

    uint32_t saved_r0, der;
    int ret = bdm_read_r0(&saved_r0);
    if (!ret)
        return false;

    ret = bdm_write_spr(SPR_LCTRL2, 0);
    if (!ret)
        goto restore_r0;

    ret = bdm_write_spr(SPR_LCTRL1, 0);
    if (!ret)
        goto restore_r0;

    ret = bdm_read_spr(SPR_DER, &der);
    if (!ret)
        goto restore_r0;

    ret = bdm_write_spr(SPR_DER, der & ~PPC_BIT(28));

restore_r0:
    int restored = bdm_restore_r0(saved_r0);

    return ret && restored;
}

bool bdm_watchpoint_hit(uint32_t address)
{
    if (!gpio_get(BDM_FRZ))
        return false;

    uint32_t saved_r0, icr, bar;
    int ret = bdm_read_r0(&saved_r0);
    if (!ret)
        return false;

    ret = bdm_read_spr(SPR_ICR, &icr);
    if (!ret)
        goto restore_r0;

    ret = bdm_read_spr(SPR_BAR, &bar);
    if (!ret)
        goto restore_r0;
    ret = (icr & PPC_BIT(28)) && bar == address;

restore_r0:
    int restored = bdm_restore_r0(saved_r0);

    return ret && restored;
}

static bool bdm_prepare_resume(void)
{
    uint32_t ictrl; 
    uint32_t value;

    if (!bdm_read_spr(SPR_ICTRL, &ictrl))
        return false;

    // When ICTRL[ISCT_SER] != 3'b111, the CPU is in serialized instead of 
    // pipeline execution mode and/or it forces some or all instruction fetches to be
    // visible on the external bus (instead of caching them.) This slows down the
    // CPU down to crawl.

    // If you enter debug mode immediately after reset, ISCT_SER != 111, so 
    // set them when resuming.
    if (halted_at_reset && (ictrl & 7) != 7) {
        ictrl |= 7;         // ICTRL[ISCT_SER] = 111
        if (!bdm_write_spr(SPR_ICTRL, ictrl))
            return false;
    }

    // Don't immediately hit the same instruction breakpoint as the one that
    // got us here.
    // Get the resume PC, compare it against the PC values that are in the
    // CMPA-CMPD breakpoint registers, and ignore the first instruction breakpoint
    // match.
    //
    // Note: we're check for address equality configuration but doesn't check if the 
    // comparators are configured for equality.
    // It won't catch cases where address range breakpoints or watchpoints are used.
    if (ictrl & 0x00000ff0) { 
        uint32_t pc;

        if (!bdm_read_spr(SPR_SRR0, &pc))
            return false;

        for (unsigned slot = 0; slot < 4; ++slot) {
            if (!(ictrl & (PPC_BIT(20 + slot) | PPC_BIT(24 + slot))))
                continue;

            if (!bdm_read_spr(SPR_CMPA + slot, &value))
                return false;

            if (value == pc) {
                // Ignore the first match so RFI can execute the stopped instruction.
                if (!bdm_write_spr(SPR_ICTRL, ictrl | PPC_BIT(28)))
                    return false;
                break;
            }
        }
    }

    // Clear DER[14]: trace exception debug enable
    int ret = bdm_read_spr(SPR_DER, &value);
    if (!ret)
        return false;

    // 31 -> DPIE: Development Port Interrupt Enable. BDM to interrupt a CPU at random.
    // 14 -> TRE : Trace Interrupt Enable. 
    ret = bdm_write_spr(SPR_DER, (value & ~PPC_BIT(14)) | PPC_BIT(31) );
    if (!ret)
        return false;

    // Clear single-step and branch tracing
    ret = bdm_read_spr(SPR_SRR1, &value);
    if (!ret)
        return false;

    // 21 -> SE: Single-Step Enable
    // 22 -> BE: Branch Trace Enable
    ret = bdm_write_spr(SPR_SRR1, value & ~(PPC_BIT(21) | PPC_BIT(22)));
    if (!ret)
        return false;

    // Invalidate the instruciton cache to avoid issues when firmware was patched.
    ret = bdm_write_spr(SPR_IC_CST, 0x0c000000);
    if (!ret)
        return false;

    // Read ICR to clear the pending debug cause.
    ret = bdm_instruction(bdm_spr_instruction(SPR_ICR, false));
    return ret;
}

bool bdm_request_resume(void)
{
    int ret = bdm_wait_until_freeze(BDM_FRZ_TIMEOUT_US);
    if (!ret)
        return false;

    ret = bdm_wait_until_ready(1000);
    if (!ret)
        return false;

    uint32_t saved_r0;
    ret = bdm_read_r0(&saved_r0);
    if (!ret)
        return false;

    bool prepared = bdm_prepare_resume();

    // Restore R0 even when preparation fails.
    bool restored = bdm_restore_r0(saved_r0);
    if (!prepared || !restored)
        return false;

    // RFI kicks the CPU out of debug mode.
    ret = bdm_instruction(PPC_RFI);
    if (!ret)
        return false;

    halted_at_reset = false;
    return true;
}

bool bdm_resume_cpu()
{
    int ret = bdm_request_resume();
    if (!ret)
        return false;

    absolute_time_t resume_deadline = make_timeout_time_us(1000);
    while (gpio_get(BDM_FRZ)) {
        if (time_reached(resume_deadline))
            return false;
        tight_loop_contents();
    }
    return true;
}

bool bdm_wait_for_watchpoint(uint32_t address, uint32_t timeout_ms)
{
    absolute_time_t resume_deadline     = make_timeout_time_us(1000);
    absolute_time_t timeout_deadline    = make_timeout_time_ms(timeout_ms);

    // This loop waits for FRZ to go low (the CPU is running normally),
    // but if that doesn't happen after some time, it can be that FRZ
    // was low for such a short time that we missed it.
    //
    // We can not just remove it altogether though: if we don't have the 
    // first loop, there could be a case FRZ is still high  because the resume 
    // didn't happen yet, but we think that the watchpoint has already triggered.
    while (gpio_get(BDM_FRZ)) {
        if (time_reached(resume_deadline) || time_reached(timeout_deadline))
            break;
        tight_loop_contents();
    }

    while (!gpio_get(BDM_FRZ)) {
        if (time_reached(timeout_deadline))
            return false;
        tight_loop_contents();
    }

    int ret = bdm_watchpoint_hit(address);
    return ret;
}

bool bdm_wait_for_instruction_breakpoint(uint32_t address, uint32_t timeout_ms)
{
    absolute_time_t resume_deadline = make_timeout_time_us(1000);
    absolute_time_t timeout_deadline = make_timeout_time_ms(timeout_ms);

    // Allow FRZ to drop, but also accept a breakpoint hit too fast to observe.
    while (gpio_get(BDM_FRZ)) {
        if (time_reached(resume_deadline) || time_reached(timeout_deadline))
            break;
        tight_loop_contents();
    }

    while (!gpio_get(BDM_FRZ)) {
        if (time_reached(timeout_deadline))
            return false;

        // Boot and decompression take seconds. Keep USB and boot logs moving.
        tud_task();
        vxw_uart_process();
    }

    int ret = bdm_instruction_breakpoint_hit(address);
    return ret;
}

bool bdm_wait_until_freeze(uint32_t timeout_us)
{
    absolute_time_t timeout_deadline = make_timeout_time_us(timeout_us);

    while(gpio_get(BDM_FRZ) == 0){
        if (time_reached(timeout_deadline))
            return false;
        tight_loop_contents();
    }

    return true;
}

bool bdm_wait_until_ready(uint32_t timeout_us)
{
    absolute_time_t timeout_deadline = make_timeout_time_us(timeout_us);

    while(gpio_get(BDM_DSDO)==1){
        if (time_reached(timeout_deadline))
            return false;
        tight_loop_contents();
    }

    return true;
}

bool bdm_halt_cpu(void)
{
    // Values not used
    uint8_t     status;
    uint32_t    rx;

    // Not sure why this is needed, but without this, calling bdm_halt_cpu wouldn't
    // work the first time you call it.
    if (!bdm_xfer(BDM_MODE_TRAP_COMMAND, BDM_CNTRL_COMMAND, BDM_CMD_CLEAR_BREAK_REQUESTS, &status, &rx, 1000))
        return false;

    // Nonmaskable breakpoint request
    bool sent = bdm_xfer(BDM_MODE_TRAP_COMMAND, BDM_CNTRL_COMMAND, BDM_CMD_ASSERT_NONMASKABLE_BREAK, &status, &rx, 1000);
    uint8_t assert_status = status;

    bool halted = sent && bdm_wait_until_freeze(BDM_FRZ_TIMEOUT_US);

    // Release the request, even after a timeout.
    bool released = bdm_xfer(BDM_MODE_TRAP_COMMAND, BDM_CNTRL_COMMAND, BDM_CMD_CLEAR_BREAK_REQUESTS, &status, &rx, 1000);
    uint8_t clear_status = status;

    #if 0
    char str[64];
    snprintf(str, sizeof(str), ">>> assert_status=%d, clear_status=%d\n\r", assert_status, clear_status);
    vxw_log(str);
    #endif

    if (halted)
        halted_at_reset = false;

    return sent && halted && released;
}

// FIXME: this should eventually be changed to use PIO, but it's good enough
// for minor firmware patching and such.
bool bdm_xfer(bool mode, bool cntrl, uint32_t tx_data, uint8_t *s, uint32_t *rx_data, uint32_t timeout_us)
{
    uint8_t     status;
    int         data_size = mode == 1 ? 7 : 32;

    if (!bdm_wait_until_ready(timeout_us)){
        *s = BDM_STATUS_TIMEOUT;
        return false;
    }

    gpio_put(BDM_DSDI, 0);
    gpio_put(BDM_DSCK, 0);
    wait_100ns();

    gpio_put(BDM_DSCK, 1);
    wait_100ns();
    gpio_put(BDM_DSCK, 0);
    gpio_put(BDM_DSDI, 1);              // START
    wait_100ns();

    gpio_put(BDM_DSCK, 1);
    wait_100ns();
    gpio_put(BDM_DSCK, 0);
    gpio_put(BDM_DSDI, mode);           // MODE
    wait_100ns();
    status  = gpio_get(BDM_DSDO) << 1;  // S<0>

    gpio_put(BDM_DSCK, 1);
    wait_100ns();
    gpio_put(BDM_DSCK, 0);
    gpio_put(BDM_DSDI, cntrl);          // CNTRL
    wait_100ns();
    status  = status | gpio_get(BDM_DSDO);  // S<1>

    *rx_data = 0;
    for(int i=data_size-1; i>=0; --i){
        gpio_put(BDM_DSCK, 1);
        wait_100ns();
        gpio_put(BDM_DSCK, 0);
        gpio_put(BDM_DSDI, (tx_data >> i) & 1);             // D<31-i>
        wait_100ns();
        *rx_data = (*rx_data << 1) | (uint32_t)gpio_get(BDM_DSDO);
    }

    gpio_put(BDM_DSCK, 1);
    wait_100ns();
    gpio_put(BDM_DSCK, 0);
    wait_100ns();

    *s = status;
    return true;

}

