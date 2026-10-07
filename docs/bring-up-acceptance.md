# Bring-up acceptance: the readings the six items are waiting on

Each of these is now instrumented, so the bench step is a **readout** with a stated expected value
rather than an experiment. The pass/fail criteria are given so the check cannot drift into "it
looked plausible".

## #13 — the strobe acknowledgement path

**Automated:** `StrobeAck_SelfTest_*` runs once at bring-up, raising a strobe and watching for the
acknowledgement, with three attempts (a chirp already in flight legitimately drops one).

| Telemetry | Means |
|---|---|
| `StrobeAckPath:ok` | the FPGA answered; the wire and the ACK logic work |
| `StrobeAckPath:FAILED` | three attempts, no acknowledgement — the wire or the FPGA is not answering |
| `StrobeAckPath:testing` | still trying (only at the very start) |

**Bench:** issue two strobes back to back so the FPGA is mid-chirp for the second, and confirm the
miss counter advances in the USB status packet — reject count at **byte 22**, `reject_seen` at
**bit 5 of byte 24**. That is the functional half this issue exists for.

## #14 — the AD9484 serial port

**Automated:** the bring-up self-test reads two registers whose defaults the datasheet states and
writes nothing.

| Telemetry | Expected on a healthy part |
|---|---|
| `ADC_SPI` | `ok` |
| `ADC_OVR` | `0x01` |
| (logged) `CHIP_PORT_CONFIG` | `0x18` |

A dead bus reads all-zero or all-one; either way `ADC_SPI` goes to `FAILED` and both values are in
the log, so the failure is a number rather than an absence.

**Bench:** use the test-pattern path (`0x0D` plus a user pattern) to prove the capture chain end to
end without RF, then check the sample format. Note the latent trap: `SJ1` straps `SCLK/DFS` to a
1.8 V rail, which selects **twos complement**, while the FPGA assumes offset binary. If the
captured samples show a half-scale error, that strap is the first thing to check.

## #5 — the eight temperature channels

| Telemetry | Expected |
|---|---|
| `T1..T8` | room temperature on a bench unit, all eight within a few degrees of each other |
| one channel reading far below the others | that probe is **unplugged** (it reads the rail); the firmware keeps the last good value and flags it |

Sanity check against the model in `docs/temperature-channels.md`: a ratio of 0.5 must read
**25.0 °C**, and 0.10 must read **95.3 °C**.

**Bench:** warm one probe by hand and confirm its channel — and only its channel — moves.

## #20 — the ADAR supply order

The order itself is asserted by the firmware and locked by `test_adar_power_order.c`. The rails
cannot be sensed today: `U6`/`U7`/`U13` are TPS562208 bucks with **no PG pin**. Specified, not
implemented: two MCU ADC channels with dividers on `+3V3_ADAR12` and `-5V0_ADAR12`, sampled during
the sequence. Until then this is a scope check on the two rails against `EN_P_3V3_ADAR12` /
`EN_P_5V0_ADAR12`.

## #7 — the harmonic filter

The brief characterizes the PA's harmonics as plots (2nd/3rd versus P_OUT, temperature, V_D and
I_DQ). The filter no longer depends on reading them: the requirement is absolute, and a catalog
part meets it — see `docs/harmonic-filter.md`.

## #19 — the OCXO inrush

Inrush into the 88 µF of `+3V3_XO` bulk is bounded by the TPS7A8300's current limit (about 0.1 ms
to 3.3 V at 3 A), but a sustained warm-up surge is **not** covered by the bulk: 500 mA for 1 ms
droops 5.7 V. So the capture is: probe `+3V3_XO` at power-up and confirm the droop stays inside the
OCXO's specification. The ECOC-2522's warm-up current has to be read from its datasheet table
(which is image-based, so it does not extract) or requested from ECS.
