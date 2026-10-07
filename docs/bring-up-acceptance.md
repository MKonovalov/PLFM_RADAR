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

## #17 — the AD9523's output spectrum (extracted from the firmware's own configuration)

The acceptance is "no unexplained clock content in the spectrum". That needs an *expected* spectrum
to compare against, and the firmware defines it. PLL2 runs the VCO at 3.6 GHz from the 100 MHz VCXO,
and these channels are enabled with these dividers:

| Channels | Divider | Output |
|---|---|---|
| 0, 1 | /12 | **300 MHz** |
| 4, 5 | /9 | **400 MHz** |
| 6 | /36 | **100 MHz** |
| 7 | /180 | **20 MHz** |
| 8, 9 | /60 | **60 MHz** |
| 10, 11 | /30 | **120 MHz** |

**Channels 2, 3, 12 and 13 are left disabled** — and 2 and 3 are OUT2 and OUT3, the two pairs whose
nets carry nothing but the driver.

So the pass criterion is two-sided, which is what makes it checkable:

1. **present:** 300, 400, 100, 20, 60 and 120 MHz;
2. **absent:** any content attributable to channels 2, 3, 12, 13 — i.e. 3.6 GHz divided by their
   dividers, and in particular nothing at the OUT2/OUT3 pin frequencies.

`AD9523_VerifyOutputs()` reads the channel registers back and reports which are powered down, so the
register side is already confirmed; the spectrum check is the physical confirmation.

## #20 — the power-up order, as a trace to compare against

The acceptance is "scope capture of the rails at power-up showing the intended order". The firmware
states the order, with delays, in the ADAR1000 bring-up:

```
t0          EN_P_3V3_ADAR12 + EN_P_3V3_ADAR34 asserted   -> +3V3_ADAR12/34 rise
t0 + 500 ms EN_P_5V0_ADAR asserted                       -> +5V0_ADAR rises
                                                            -> -5V0_ADAR12/34 follow (U20/U21/U36/U37)
t0 + 1000ms sequencing complete
```

The expected trace, and the two things to look for:

1. **+3V3_ADAR12/34 rise before (or with) −5V0_ADAR12/34.** This is the ADAR1000 pin-table
   requirement, quoted in the firmware beside the code.
2. **−5V0_ADAR12/34 cannot precede +5V0_ADAR.** They are charge-pump inversions of it, so the
   negative has nothing to invert until the positive exists — the order is structural, not
   sequential, and `9_Firmware/tools/power_sequencing.py` enforces the pairing.

The same structure holds for the switch rails: `+3V3_SW` (staged on MCU `PE14`) precedes `−3V3_SW`
(its pump), matching the ADTR1107's documented sequence — VDD_SW 3.3 V, then VSS_SW −3.3 V.

## #14 — the test-pattern check, with expected values

The acceptance asks for the capture chain to be proven without RF. The AD9484 is an **8-bit**,
500 MSPS part with a 1.5 V full scale, and its test-pattern modes (register `0x0D` TEST_IO, bits
`[3:0]`) let the output be driven with a known value.

**The self-checking case is the one to use.** Load the user-defined pattern and read it back:

```
AD9484_SPI_SetTestPattern(&io, true, 0xA5);   -> 0x19 / 0x1A hold it, 0x0D selects it
captured samples must all read 0xA5
```

The expected value is known from the register rather than from a graph, so this needs no external
reference — and it exercises the whole chain: SPI write, the DEVICE_UPDATE transfer, the pattern
generator, the LVDS output, the FPGA capture and the sample format.

**The modes with derivable codes** (in the configured **offset binary** format — note that midscale
is `0x80`, not `0x00`, which is why the firmware forces the format rather than trusting the `SJ1`
strap):

| Mode (0x0D bits[3:0]) | Expected output |
|---|---|
| `0001` midscale short | `0x80` |
| `0010` +FS short | `0xFF` |
| `0011` −FS short | `0x00` |
| `0100` checkerboard | alternating `0x55` / `0xAA` |
| `0111` one/zero word toggle | alternating `0xFF` / `0x00` |
| `1000` user-defined | whatever `0x19`/`0x1A` hold |

`0101` (PN23) and `0110` (PN9) are pseudo-random sequences: they can be checked against the
generator polynomial, but the user-defined case above is the simpler proof that the chain works.

The datasheet names the patterns and states the resolution; the codes in the table are **derived**
from the pattern definitions and the selected format, which is why the user-defined row is the one
worth relying on.
