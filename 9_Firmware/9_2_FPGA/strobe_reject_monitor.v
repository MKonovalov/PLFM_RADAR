// strobe_reject_monitor.v -- makes dropped STM32 strobes visible to the host
//
// The chirp FSM consumes a `new_chirp` strobe only in IDLE:
//
//     IDLE: if (chirp__toggling && mixers_enable) next_state = LONG_CHIRP;
//
// Everywhere else the strobe is not read at all, so a command that arrives
// mid-sequence is dropped silently.  Nothing in the current design tells the
// MCU or the host that it happened, which is what makes it worth instrumenting:
// the MCU never learns that its command was ignored.  (The MCU already has the
// frame boundary available, so the fix on that side is to send the strobe when
// the boundary says the FSM is idle.)
//
// This module counts those drops and presents them the way a host can use them:
//
//   reject_count  rejections seen during the last *complete* frame.  A rejection
//                 during the frame in progress becomes visible after the next
//                 frame boundary, so the value never changes while the host is
//                 reading it and it self-clears (a clean frame reports 0) --
//                 no clear-on-read opcode needed.
//   reject_seen   sticky for as long as reset is released: "has this ever
//                 happened".  Also needs no clear.
//
// A host-side clear is deliberately absent; adding one would need a new
// readback opcode and a cross-domain path to the 100 MHz domain.

module strobe_reject_monitor (
    input  wire       clk,              // clk_100m
    input  wire       reset_n,
    input  wire       reject_pulse,     // 1-cycle: a strobe arrived while busy
    input  wire       frame_boundary,   // 1-cycle: a new frame just started
    output reg  [7:0] reject_count,     // rejections in the last complete frame
    output reg        reject_seen       // sticky, cleared only by reset
);

    reg [7:0] count_this_frame;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            count_this_frame <= 8'd0;
            reject_count     <= 8'd0;
            reject_seen      <= 1'b0;
        end else begin
            if (reject_pulse) begin
                reject_seen <= 1'b1;
                // Saturate rather than wrap: a wrapped count would look like a
                // quiet frame exactly when the link is worst.
                if (count_this_frame != 8'hFF)
                    count_this_frame <= count_this_frame + 8'd1;
            end

            if (frame_boundary) begin
                reject_count     <= count_this_frame;
                count_this_frame <= 8'd0;
            end
        end
    end

endmodule
