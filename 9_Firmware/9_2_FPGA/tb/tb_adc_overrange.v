`timescale 1ns / 1ps
// ============================================================================
// tb_adc_overrange.v -- the AD9484 out-of-range flag must reach the AGC path
// ============================================================================
// The ADC's OR pin reports *analog* overload; the digital clip count only sees
// the post-digital-gain value.  Until this change the OR pin was wired to the
// FPGA but absent from the RTL and the XDC, so a real front-end overload the
// digital AGC had already backed off was invisible.
//
// This test drives the OR input of radar_receiver_final through the simulation
// stub (which models the interface's IBUFDS as a single-ended level) and checks
// the published status:
//   [PASS] no overrange -> adc_overrange_seen low, count 0
//   [PASS] OR asserted  -> adc_overrange_seen high, count increments, saturates
//   [PASS] frame pulse  -> seen and count clear
// ============================================================================

module tb_adc_overrange;

    reg         clk = 0;
    reg         reset_n = 0;
    reg         adc_or_p = 0;
    reg         tx_frame_start = 0;
    reg  [7:0]  adc_d_p = 8'h00;
    reg         adc_dco_p = 0;

    wire [7:0]  adc_overrange_count;
    wire        adc_overrange_seen;

    integer pass_count = 0;
    integer fail_count = 0;

    task check;
        input        cond;
        input [255:0] name;
        begin
            if (cond) begin
                $display("[PASS] %0s", name);
                pass_count = pass_count + 1;
            end else begin
                $display("[FAIL] %0s", name);
                fail_count = fail_count + 1;
            end
        end
    endtask

    // 100 MHz system clock, 400 MHz ADC DCO
    always #5.0  clk = ~clk;
    always #1.25 adc_dco_p = ~adc_dco_p;

    radar_receiver_final dut (
        .clk            (clk),
        .reset_n        (reset_n),
        .adc_d_p        (adc_d_p),
        .adc_d_n        (8'h00),
        .adc_dco_p      (adc_dco_p),
        .adc_dco_n      (1'b0),
        .adc_or_p       (adc_or_p),
        .adc_or_n       (1'b0),
        .chirp_counter  (6'd0),
        .tx_frame_start (tx_frame_start),
        .host_mode      (2'b00),
        .host_trigger   (1'b0),
        .host_long_chirp_cycles  (16'd3000),
        .host_long_listen_cycles (16'd1000),
        .host_guard_cycles       (16'd100),
        .host_short_chirp_cycles (16'd300),
        .host_short_listen_cycles(16'd100),
        .host_chirps_per_elev    (6'd32),
        .host_gain_shift         (4'd0),
        .host_agc_enable         (1'b1),
        .host_agc_target         (8'd64),
        .host_agc_attack         (4'd4),
        .host_agc_decay          (4'd2),
        .host_agc_holdoff        (4'd1),
        .stm32_new_chirp_rx      (1'b0),
        .stm32_new_elevation_rx  (1'b0),
        .stm32_new_azimuth_rx    (1'b0),
        .host_mti_enable         (1'b0),
        .host_dc_notch_width     (3'd0),
        .adc_overrange_count     (adc_overrange_count),
        .adc_overrange_seen      (adc_overrange_seen)
    );

    initial begin
        // reset
        repeat (10) @(posedge clk);
        reset_n = 1;
        repeat (10) @(posedge clk);

        check(adc_overrange_seen === 1'b0, "idle: no overrange reported");
        check(adc_overrange_count === 8'd0, "idle: count is zero");

        // assert the analog out-of-range flag for 20 sample clocks
        adc_or_p = 1'b1;
        repeat (20) @(posedge clk);
        check(adc_overrange_seen === 1'b1, "OR asserted: seen flag raised");
        check(adc_overrange_count > 8'd0, "OR asserted: count incremented");

        // hold it long enough to prove the counter saturates instead of wrapping
        repeat (300) @(posedge clk);
        check(adc_overrange_count === 8'hFF, "saturates at 0xFF, does not wrap");

        // release the flag; the per-frame pulse must clear the per-frame status
        adc_or_p = 1'b0;
        @(posedge clk);
        tx_frame_start = 1'b1;
        @(posedge clk);
        tx_frame_start = 1'b0;
        repeat (6) @(posedge clk);
        check(adc_overrange_seen === 1'b0, "frame pulse clears the seen flag");
        check(adc_overrange_count === 8'd0, "frame pulse clears the count");

        $display("=== Results: %0d passed, %0d failed ===", pass_count, fail_count);
        if (fail_count == 0) $display("[PASS] tb_adc_overrange complete");
        else                 $display("[FAIL] tb_adc_overrange had failures");
        $finish;
    end

    initial begin
        #200000;
        $display("[FAIL] tb_adc_overrange TIMEOUT");
        $finish;
    end

endmodule
