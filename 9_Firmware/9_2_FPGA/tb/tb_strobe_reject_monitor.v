// tb_strobe_reject_monitor.v -- unit tests for the dropped-strobe monitor
//
// The monitor is the host-visible half of the strobe-acknowledgement item: the
// chirp FSM silently drops a `new_chirp` strobe that arrives mid-sequence, and
// nothing else in the design notices.  These checks pin the properties a host
// reading the status packet depends on:
//
//   * a clean frame reports 0 (the value self-clears, no clear opcode exists)
//   * rejections appear only at a frame boundary, so the value is stable while read
//   * the count saturates instead of wrapping
//   * "has this ever happened" is sticky across clean frames

`timescale 1ns / 1ps

module tb_strobe_reject_monitor;

    reg        clk = 0;
    reg        reset_n = 0;
    reg        reject_pulse = 0;
    reg        frame_boundary = 0;

    wire [7:0] reject_count;
    wire       reject_seen;

    integer tests_passed = 0;
    integer tests_total  = 0;

    strobe_reject_monitor dut (
        .clk            (clk),
        .reset_n        (reset_n),
        .reject_pulse   (reject_pulse),
        .frame_boundary (frame_boundary),
        .reject_count   (reject_count),
        .reject_seen    (reject_seen)
    );

    // 100 MHz
    always #5 clk = ~clk;

    task tick(input integer n);
        integer i;
        begin
            for (i = 0; i < n; i = i + 1) @(posedge clk);
        end
    endtask

    task one_reject;
        begin
            reject_pulse = 1'b1;
            @(posedge clk);
            reject_pulse = 1'b0;
            @(posedge clk);
        end
    endtask

    task one_boundary;
        begin
            frame_boundary = 1'b1;
            @(posedge clk);
            frame_boundary = 1'b0;
            @(posedge clk);
        end
    endtask

    task check(input [8:0] got, input [8:0] expected, input [255:0] tag);
        begin
            tests_total = tests_total + 1;
            if (got === expected) begin
                tests_passed = tests_passed + 1;
                $display("[PASS] case %0d: got %0d", tag, got);
            end else begin
                $display("[FAIL] case %0d: got %0d, expected %0d", tag, got, expected);
            end
        end
    endtask

    integer i;

    initial begin
        $dumpfile("tb_strobe_reject_monitor.vcd");
        $dumpvars(0, tb_strobe_reject_monitor);

        // ---- 1. reset state is quiet --------------------------------------
        reset_n = 0;
        tick(4);
        reset_n = 1;
        tick(2);
        check({reject_seen, reject_count}, 9'd0, 1);

        // ---- 2. a clean frame reports zero --------------------------------
        one_boundary;
        check({reject_seen, reject_count}, 9'd0, 2);

        // ---- 3. a rejection is not reported until the frame ends ----------
        one_reject;
        one_reject;
        check(reject_count, 8'd0, 3);          // still the previous frame's value
        one_boundary;
        check(reject_count, 8'd2, 4);
        check({1'b0, reject_seen}, 9'd1, 5);    // sticky bit is up

        // ---- 4. the value self-clears on the next clean frame -------------
        one_boundary;
        check(reject_count, 8'd0, 6);
        check({1'b0, reject_seen}, 9'd1, 7);    // but "seen" stays latched

        // ---- 5. a boundary alone never invents rejections -----------------
        one_boundary;
        one_boundary;
        check(reject_count, 8'd0, 8);

        // ---- 6. saturation, not wrap --------------------------------------
        for (i = 0; i < 300; i = i + 1)
            one_reject;
        one_boundary;
        check(reject_count, 8'd255, 9);
        one_boundary;
        check(reject_count, 8'd0, 10);          // the window still resets

        // ---- 7. reset clears the sticky bit -------------------------------
        reset_n = 0;
        tick(4);
        reset_n = 1;
        tick(2);
        check({reject_seen, reject_count}, 9'd0, 11);

        $display("=== Results: %0d/%0d passed ===", tests_passed, tests_total);
        if (tests_passed != tests_total)
            $display("*** FAILED ***");
        $finish;
    end

endmodule
