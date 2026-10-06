/* Gate-bias emergency-stop contract, tested against the REAL DAC5578 driver.
 *
 * Why this file exists: the driver decides what an asserted CLR does to 16
 * depletion-mode GaN gates, and until now it was compiled by nothing on the host side,
 * so the emergency-stop path could not be tested at all.  The mock needed the I2C HAL
 * added first (HAL_I2C_Master_Transmit / Receive); with that in place this links the
 * actual DA5578.c and checks the behaviour rather than a copy of it.
 *
 * The regression it pins down: an emergency stop must NOT drive the gates to 0 V.
 * The driver used to be configured for zero-scale, which does exactly that.
 */
#include "stm32_hal_mock.h"
#include "DAC5578.h"
#include "PA_GATE_BIAS.h"

#include <stdio.h>

/* Compile-time: the driver's enum and the gate-bias contract must agree.  If either
 * side is renumbered, the build stops here rather than at a destroyed PA array. */
typedef char chk_clearcode_zero[(DAC5578_CLR_CODE_ZERO == PA_GATE_BIAS_CLEAR_CODE_ZERO) ? 1 : -1];
typedef char chk_clearcode_mid [(DAC5578_CLR_CODE_MID  == PA_GATE_BIAS_CLEAR_CODE_MID ) ? 1 : -1];
typedef char chk_clearcode_full[(DAC5578_CLR_CODE_FULL == PA_GATE_BIAS_CLEAR_CODE_FULL) ? 1 : -1];
typedef char chk_clearcode_nop [(DAC5578_CLR_CODE_NOP  == PA_GATE_BIAS_CLEAR_CODE_NOP ) ? 1 : -1];

#define RESET_OPCODE (0x8 << 4)   /* DAC5578_CMD_RESET */

static int checks = 0;
static int failures = 0;

static void check(int ok, const char *what)
{
    checks++;
    if (!ok) {
        failures++;
        printf("   FAIL: %s\n", what);
    }
}

static DAC5578_HandleTypeDef *fresh_handle(void)
{
    static DAC5578_HandleTypeDef h;
    h.hi2c = &hi2c1;
    h.i2c_addr = (uint8_t)(0x48 << 1);
    h.resolution_bits = 8;
    h.clear_code = DAC5578_CLR_CODE_MID;
    return &h;
}

int main(void)
{
    printf("=== an emergency stop must not turn the PAs on ===\n");

    /* --- zero-scale is refused, and it never reaches the bus ------------------ */
    spy_reset();
    DAC5578_HandleTypeDef *h = fresh_handle();
    int ok = DAC5578_SetClearCode(h, DAC5578_CLR_CODE_ZERO) ? 1 : 0;
    check(!ok, "zero-scale clear code is refused");
    check(spy_count_type(SPY_I2C_TX) == 0,
          "the refusal happens before any I2C traffic (bus untouched)");
    check(h->clear_code != DAC5578_CLR_CODE_ZERO, "the handle keeps a safe setting after the refusal");

    /* --- retain-last is refused too: its outcome is unknown ------------------ */
    spy_reset();
    ok = DAC5578_SetClearCode(h, DAC5578_CLR_CODE_NOP) ? 1 : 0;
    check(!ok, "retain-last clear code is refused");
    check(spy_count_type(SPY_I2C_TX) == 0, "and touches no bus");

    /* --- mid-scale is accepted, and the wire carries what we think it does --- */
    spy_reset();
    ok = DAC5578_SetClearCode(h, DAC5578_CLR_CODE_MID) ? 1 : 0;
    check(ok, "mid-scale clear code is accepted");
    check(h->clear_code == DAC5578_CLR_CODE_MID, "handle records mid-scale");
    check(DAC5578_GetClearCode(h) == DAC5578_CLR_CODE_MID, "and reads back as mid-scale");
    check(spy_count_type(SPY_I2C_TX) == 1, "exactly one transfer was issued");
    if (spy_count_type(SPY_I2C_TX) == 1) {
        check(mock_i2c_last_tx_len == 3, "the reset command is 3 bytes");
        check(mock_i2c_last_tx[0] == RESET_OPCODE, "opcode byte is CMD_RESET");
        check((mock_i2c_last_tx[2] & 0x03) == PA_GATE_BIAS_CLEAR_CODE_MID,
              "clear-code bits on the wire encode mid-scale");
        check(mock_i2c_last_addr == (0x48 << 1), "addressed to the DAC, shifted as the HAL wants");
    }
    /* the semantics of that wire value: the state CLR produces is OFF */
    check(PA_GATE_BIAS_VggFromCode(PA_GATE_BIAS_CODE_OFF) <= PA_GATE_BIAS_VGG_MAX_V,
          "what CLR will command is at or below the rated gate edge (i.e. off)");

    /* --- full-scale is also a parking state (the op-amp clamps it) ----------- */
    spy_reset();
    ok = DAC5578_SetClearCode(h, DAC5578_CLR_CODE_FULL) ? 1 : 0;
    check(ok, "full-scale clear code is accepted (gates off, clamped by the op-amp)");
    check(h->clear_code == DAC5578_CLR_CODE_FULL, "handle records full-scale");

    /* --- Init must default to a fail-safe clear code ------------------------- */
    spy_reset();
    DAC5578_HandleTypeDef hd;
    memset(&hd, 0, sizeof(hd));
    int init_ok = DAC5578_Init(&hd, &hi2c2, 0x49, 8, GPIOB, GPIO_PIN_8, GPIOB, GPIO_PIN_9);
    check(init_ok == 1, "Init succeeds against the mock HAL");
    check(PA_GATE_BIAS_ClearCodeIsFailSafe((int)hd.clear_code),
          "Init leaves the handle in a fail-safe clear-code state");
    check(hd.clear_code == DAC5578_CLR_CODE_MID, "specifically mid-scale");

    /* --- a bus failure must be reported, not silently ignored ---------------- */
    spy_reset();
    mock_i2c_status = HAL_ERROR;
    ok = DAC5578_SetClearCode(h, DAC5578_CLR_CODE_MID) ? 1 : 0;
    check(!ok, "a failing bus reports failure (the caller can refuse to arm the PAs)");
    mock_i2c_status = HAL_OK;

    /* --- the same driver setting must hold for both DACs --------------------- */
    check(PA_GATE_BIAS_ClearCodeIsFailSafe(DAC5578_CLR_CODE_MID) &&
          PA_GATE_BIAS_ClearCodeIsFailSafe(DAC5578_CLR_CODE_FULL) &&
          !PA_GATE_BIAS_ClearCodeIsFailSafe(DAC5578_CLR_CODE_ZERO) &&
          !PA_GATE_BIAS_ClearCodeIsFailSafe(DAC5578_CLR_CODE_NOP),
          "the contract's own verdict on all four clear-code settings");

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
