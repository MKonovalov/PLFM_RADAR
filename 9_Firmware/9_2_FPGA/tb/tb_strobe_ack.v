// tb_strobe_ack.v
//
// Issue #13: the acknowledgement protocol the MCU polls.
//
// The property under test is the one the MCU relies on: after it raises a strobe, ack is low
// until the design consumes it, and a strobe that is never consumed leaves ack low - which is
// how a dropped request becomes visible on a single wire.

`timescale 1ns / 1ps

module tb_strobe_ack;

    reg  clk = 0, rst = 1, strobe = 0, consumed = 0;
    wire ack;

    integer pass_count = 0, fail_count = 0;

    strobe_ack dut (.clk(clk), .rst(rst), .strobe(strobe), .consumed(consumed), .ack(ack));

    always #5 clk = ~clk;   // 100 MHz

    task check(input cond, input [8*80-1:0] what);
        begin
            if (cond) begin
                $display("[PASS] %0s", what);
                pass_count = pass_count + 1;
            end else begin
                $display("[FAIL] %0s (ack=%b strobe=%b)", what, ack, strobe);
                fail_count = fail_count + 1;
            end
        end
    endtask

    initial begin
        $display("=== strobe_ack: the MCU's view of a request ===");

        // reset
        @(negedge clk); rst = 1; @(negedge clk); @(negedge clk);
        check(ack === 1'b0, "reset leaves ack low");

        // a request is raised: ack must be low until it is consumed
        rst = 0;
        @(negedge clk); strobe = 1;
        @(negedge clk); @(negedge clk);
        check(ack === 1'b0, "a raised request clears the acknowledgement");

        // consumption sets it, and it stays set while the strobe is held
        consumed = 1; @(negedge clk); consumed = 0;
        @(negedge clk); @(negedge clk);
        check(ack === 1'b1, "consumption sets the acknowledgement");
        @(negedge clk); @(negedge clk);
        check(ack === 1'b1, "and it holds while the request stays asserted");

        // the MCU drops the strobe and issues the next one: ack must clear again
        strobe = 0; @(negedge clk); @(negedge clk);
        check(ack === 1'b1, "dropping the request does not clear it by itself");
        strobe = 1; @(negedge clk); @(negedge clk);
        check(ack === 1'b0, "the next request clears it again");

        // the dropped case: a request that is never consumed stays unacknowledged
        @(negedge clk); @(negedge clk); @(negedge clk);
        check(ack === 1'b0, "a request that is never consumed stays unacknowledged");

        // and it can still be consumed late
        consumed = 1; @(negedge clk); consumed = 0;
        @(negedge clk);
        check(ack === 1'b1, "a late consumption still sets it");

        // reset clears everything
        rst = 1; strobe = 0; @(negedge clk); @(negedge clk);
        check(ack === 1'b0, "reset returns it to low");

        $display("=== Results: %0d passed, %0d failed ===", pass_count, fail_count);
        if (fail_count == 0) $display("[PASS] tb_strobe_ack complete");
        else                 $display("[FAIL] tb_strobe_ack had failures");
        $finish;
    end

    initial begin
        #100000;
        $display("[FAIL] tb_strobe_ack TIMEOUT");
        $finish;
    end

endmodule
