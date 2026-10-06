`timescale 1ns / 1ps
// ============================================================================
// tb_agc_magnitude_link.v -- the magnitude link must encode severity exactly
// ============================================================================
// The MCU's AGC attack is proportional only if the pulse count it sees equals
// the severity class of the frame's saturation count.  This test drives the
// encoding directly (the module is standalone for exactly this reason) and
// counts rising edges on the output.
//
//   class = bit length of sat_count, clamped to 7;  0 -> no pulses
//
// Checks: the whole class table, idle behaviour, a second frame re-arming the
// train, and that a count changing mid-train cannot stretch it.
// ============================================================================

module tb_agc_magnitude_link;

    reg        clk = 0;
    reg        reset_n = 0;
    reg        frame_start = 0;
    reg  [7:0] sat_count = 8'd0;
    wire       mag_out;

    integer pass_count = 0;
    integer fail_count = 0;
    integer edges;

    always #5.0 clk = ~clk;   // 100 MHz

    agc_magnitude_link dut (
        .clk(clk),
        .reset_n(reset_n),
        .frame_start(frame_start),
        .sat_count(sat_count),
        .mag_out(mag_out)
    );

    // count rising edges for one frame window (long enough for the longest train)
    task run_frame;
        input [7:0] count;
        input [3:0] expect;
        input [255:0] tag;
        begin
            sat_count = count;
            @(posedge clk);
            frame_start = 1'b1;
            @(posedge clk);
            frame_start = 1'b0;
            edges = 0;
            // 8 pulses worst case at 3 us each = 24 us; watch 60 us (6000 cycles)
            repeat (6000) begin
                @(posedge clk);
                if (mag_out) begin
                    @(negedge clk);
                    if (mag_out) edges = edges + 1;   // one edge per high phase
                    // wait for the line to return low before counting again
                    while (mag_out) @(posedge clk);
                end
            end
            if (edges == expect) begin
                $display("[PASS] case %0d: count=%0d -> %0d pulse(s)", tag, count, edges);
                pass_count = pass_count + 1;
            end else begin
                $display("[FAIL] case %0d: count=%0d -> %0d pulse(s), expected %0d",
                         tag, count, edges, expect);
                fail_count = fail_count + 1;
            end
        end
    endtask

    initial begin
        repeat (10) @(posedge clk);
        reset_n = 1;
        repeat (10) @(posedge clk);

        // idle: no frame, no pulses
        edges = 0;
        repeat (1000) @(posedge clk);
        if (edges == 0 && mag_out === 1'b0) begin
            $display("[PASS] idle: line low, no pulses");
            pass_count = pass_count + 1;
        end else begin
            $display("[FAIL] idle: line not quiet");
            fail_count = fail_count + 1;
        end

        // the whole class table
        run_frame(8'd0, 4'd0, 4'd0);
        run_frame(8'd1, 4'd1, 4'd1);
        run_frame(8'd2, 4'd2, 4'd2);
        run_frame(8'd3, 4'd2, 4'd2);
        run_frame(8'd7, 4'd3, 4'd3);
        run_frame(8'd8, 4'd4, 4'd4);
        run_frame(8'd31, 4'd5, 4'd5);
        run_frame(8'd63, 4'd6, 4'd6);
        run_frame(8'd127, 4'd7, 4'd7);
        run_frame(8'd255, 4'd7, 4'd7);

        // a second frame must re-arm (the train is not one-shot)
        run_frame(8'd16, 4'd5, 4'd5);

        // a count that changes mid-train must not stretch the current train:
        // latch the class at the frame boundary, then spike the count
        sat_count = 8'd3;               // class 2
        @(posedge clk);
        frame_start = 1'b1;
        @(posedge clk);
        frame_start = 1'b0;
        sat_count = 8'd255;             // would be class 7 if it were not latched
        edges = 0;
        repeat (6000) begin
            @(posedge clk);
            if (mag_out) begin
                @(negedge clk);
                if (mag_out) edges = edges + 1;
                while (mag_out) @(posedge clk);
            end
        end
        if (edges == 2) begin
            $display("[PASS] latched: mid-train count change did not alter the train");
            pass_count = pass_count + 1;
        end else begin
            $display("[FAIL] latched: got %0d pulses, expected 2", edges);
            fail_count = fail_count + 1;
        end

        $display("=== Results: %0d passed, %0d failed ===", pass_count, fail_count);
        if (fail_count == 0) $display("[PASS] tb_agc_magnitude_link complete");
        else                 $display("[FAIL] tb_agc_magnitude_link had failures");
        $finish;
    end

    initial begin
        #2000000;
        $display("[FAIL] tb_agc_magnitude_link TIMEOUT");
        $finish;
    end

endmodule
