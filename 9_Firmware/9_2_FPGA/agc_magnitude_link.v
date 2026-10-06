`timescale 1ns / 1ps
// ============================================================================
// agc_magnitude_link.v -- sends the AGC saturation *magnitude* over one wire
// ============================================================================
// The FPGA knows how badly the receiver is clipping (an 8-bit per-frame count)
// but the MCU's outer AGC loop only ever saw two levels of it, because DIG_7
// was a single bit: "hard overload" or not.  A bang-bang attack on a 1-bit error
// signal hunts around the threshold instead of converging.
//
// This module turns the count into a pulse train on one wire: after each frame
// boundary it emits N pulses, where N is the severity class of the count:
//
//   count        1  2-3  4-7  8-15  16-31  32-63  64-127  128-255
//   class        1    2    3     4      5      6       7        7 (clamped)
//   count == 0 -> class 0, no pulses (nothing clipped, so no attack)
//
// The class is the bit length of the count, i.e. roughly log2 of how many
// samples clipped.  The MCU counts rising edges inside the frame and picks its
// attack step from a table, which makes the attack proportional to the overload
// rather than fixed.
//
// Timing (100 MHz): 1 us high, 2 us low per pulse, so the longest train is
// 7 x 3 us = 21 us after the frame pulse -- negligible against a 258 ms frame,
// and slow enough for a GPIO interrupt on the MCU to count reliably.
//
// Failure behaviour: if the MCU sees no pulses it must treat the magnitude as
// unknown and fall back to its previous fixed attack.  That keeps the link
// safe to enable before it has been validated on hardware.
// ============================================================================

module agc_magnitude_link #(
    parameter PULSE_CYCLES = 100,   // 1 us at 100 MHz -- edge is comfortably long
    parameter GAP_CYCLES   = 200,   // 2 us low between pulses
    parameter MAX_PULSES   = 7      // class ceiling; keep <= 15 for the MCU counter
)(
    input  wire       clk,
    input  wire       reset_n,
    input  wire       frame_start,  // 1-cycle pulse at the frame boundary
    input  wire [7:0] sat_count,    // per-frame clipped-sample count
    output reg        mag_out       // pulse train (DIG_7)
);

    // severity class = bit length of sat_count, clamped to MAX_PULSES
    function [3:0] severity_class;
        input [7:0] c;
        begin
            if      (c == 8'd0) severity_class = 4'd0;
            else if (c[7])      severity_class = 4'd8;
            else if (c[6])      severity_class = 4'd7;
            else if (c[5])      severity_class = 4'd6;
            else if (c[4])      severity_class = 4'd5;
            else if (c[3])      severity_class = 4'd4;
            else if (c[2])      severity_class = 4'd3;
            else if (c[1])      severity_class = 4'd2;
            else                severity_class = 4'd1;
        end
    endfunction

    localparam S_IDLE  = 2'd0,
               S_HIGH  = 2'd1,
               S_GAP   = 2'd2,
               S_FINAL = 2'd3;   // gap after the last pulse before going idle

    localparam [3:0] PULSE_MAX = MAX_PULSES;

    reg [1:0]  state;
    reg [3:0]  pulses_left;
    reg [15:0] timer;
    reg [3:0]  class_now;

    wire [3:0] class_next = severity_class(sat_count);
    wire [3:0] class_clamped = (class_next > PULSE_MAX) ? PULSE_MAX : class_next;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            state       <= S_IDLE;
            pulses_left <= 4'd0;
            timer       <= 16'd0;
            mag_out     <= 1'b0;
            class_now   <= 4'd0;
        end else begin
            case (state)
                S_IDLE: begin
                    mag_out <= 1'b0;
                    if (frame_start) begin
                        // latch the class at the frame boundary so a changing
                        // count cannot stretch or truncate the train
                        class_now   <= class_clamped;
                        pulses_left <= class_clamped;
                        timer       <= 16'd0;
                        state       <= (class_clamped == 4'd0) ? S_IDLE : S_HIGH;
                    end
                end

                S_HIGH: begin
                    mag_out <= 1'b1;
                    if (timer + 1 >= PULSE_CYCLES[15:0]) begin
                        timer <= 16'd0;
                        mag_out <= 1'b0;
                        pulses_left <= pulses_left - 4'd1;
                        state <= (pulses_left == 4'd1) ? S_FINAL : S_GAP;
                    end else begin
                        timer <= timer + 16'd1;
                    end
                end

                S_GAP: begin
                    mag_out <= 1'b0;
                    if (timer + 1 >= GAP_CYCLES[15:0]) begin
                        timer <= 16'd0;
                        state <= S_HIGH;
                    end else begin
                        timer <= timer + 16'd1;
                    end
                end

                S_FINAL: begin
                    // hold the line low for one gap, then report idle
                    mag_out <= 1'b0;
                    if (timer + 1 >= GAP_CYCLES[15:0]) begin
                        timer <= 16'd0;
                        pulses_left <= 4'd0;
                        state <= S_IDLE;
                    end else begin
                        timer <= timer + 16'd1;
                    end
                end

                default: state <= S_IDLE;
            endcase
        end
    end

endmodule
