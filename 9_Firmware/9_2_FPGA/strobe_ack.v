// strobe_ack.v
//
// Issue #13: the MCU-to-FPGA strobes have no acknowledgement.
//
// The MCU asserts new_chirp / new_elevation / new_azimuth and has no way to know whether the
// FPGA acted on one. The chirp FSM only reads new_chirp in IDLE, so a strobe that arrives
// mid-chirp is dropped - and until now the only trace of that was a counter inside the USB
// status packet, which the MCU cannot read without the host asking.
//
// This module gives the MCU a single wire it can poll:
//
//   strobe rises   -> ack clears   ("your request is not answered yet")
//   consumed pulses -> ack sets     ("the request was taken")
//
// The MCU therefore asserts a strobe, then checks that ack went high before issuing the next
// one. A dropped strobe shows up as ack still low at the next request, which the MCU counts.
//
// `consumed` is the existing internal acceptance pulse (new_chirp_frame in the DAC clock
// domain, already synchronized by the design's own CDC), so this module adds no new timing
// assumptions - it only exposes what the design already knows.

`timescale 1ns / 1ps

module strobe_ack (
    input  wire clk,
    input  wire rst,
    input  wire strobe,      // the MCU's request, synchronized to this clock domain
    input  wire consumed,    // internal pulse: the request was taken
    output reg  ack          // level: high once the request has been consumed
);

    reg strobe_d;

    always @(posedge clk) begin
        if (rst) begin
            strobe_d <= 1'b0;
            ack      <= 1'b0;
        end else begin
            strobe_d <= strobe;
            if (strobe & ~strobe_d) begin
                ack <= 1'b0;          // a new request clears the acknowledgement
            end else if (consumed) begin
                ack <= 1'b1;          // and consumption sets it
            end
        end
    end

endmodule
