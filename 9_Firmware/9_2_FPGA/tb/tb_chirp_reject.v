// tb_chirp_reject.v -- does a strobe that arrives mid-sequence actually get dropped?
//
// This drives the real `plfm_chirp_controller_enhanced` with shortened timing and
// checks the failure the strobe-acknowledgement item is about: the FSM reads
// new_chirp only in IDLE, so a command that arrives while a frame is running is
// ignored.  It is not enough for that to be reasoned about from the source -- the
// point of the change is that the drop is now *observable*, so the test asserts
// the detector fires exactly when the command is ignored and stays quiet when it
// is honoured.
//
// Timing parameters are shortened the same way tb_chirp_controller.v shortens
// them, to keep the run tractable.

`timescale 1ns / 1ps

module tb_chirp_reject;

    parameter T1_SAMPLES         = 8;
    parameter T1_RADAR_LISTENING = 4;
    parameter T2_SAMPLES         = 4;
    parameter T2_RADAR_LISTENING = 4;
    parameter GUARD_SAMPLES      = 4;
    parameter CHIRP_MAX          = 4;
    parameter ELEVATION_MAX      = 2;
    parameter AZIMUTH_MAX        = 2;

    reg clk_120m = 0;
    reg clk_100m = 0;
    reg reset_n = 0;
    reg new_chirp = 0;
    reg new_elevation = 0;
    reg new_azimuth = 0;
    reg mixers_enable = 0;

    wire [7:0] chirp_data;
    wire       chirp_valid;
    wire       new_chirp_frame;
    wire       chirp_done;
    wire       rf_switch_ctrl;
    wire       rx_mixer_en, tx_mixer_en;
    wire       adar_tx_load_1, adar_rx_load_1;
    wire       adar_tx_load_2, adar_rx_load_2;
    wire       adar_tx_load_3, adar_rx_load_3;
    wire       adar_tx_load_4, adar_rx_load_4;
    wire       adar_tr_1, adar_tr_2, adar_tr_3, adar_tr_4;
    wire [5:0] chirp_counter, elevation_counter, azimuth_counter;
    wire       chirp_reject_toggle;

    integer reject_events = 0;
    integer frame_events  = 0;
    integer tests_passed  = 0;
    integer tests_total   = 0;

    reg reject_prev = 0;
    reg frame_prev  = 0;

    plfm_chirp_controller_enhanced #(
        .T1_SAMPLES         (T1_SAMPLES),
        .T1_RADAR_LISTENING (T1_RADAR_LISTENING),
        .T2_SAMPLES         (T2_SAMPLES),
        .T2_RADAR_LISTENING (T2_RADAR_LISTENING),
        .GUARD_SAMPLES      (GUARD_SAMPLES),
        .CHIRP_MAX          (CHIRP_MAX),
        .ELEVATION_MAX      (ELEVATION_MAX),
        .AZIMUTH_MAX        (AZIMUTH_MAX)
    ) dut (
        .clk_120m        (clk_120m),
        .clk_100m        (clk_100m),
        .reset_n         (reset_n),
        .new_chirp       (new_chirp),
        .new_elevation   (new_elevation),
        .new_azimuth     (new_azimuth),
        .mixers_enable   (mixers_enable),
        .chirp_data      (chirp_data),
        .chirp_valid     (chirp_valid),
        .new_chirp_frame (new_chirp_frame),
        .chirp_done      (chirp_done),
        .rf_switch_ctrl  (rf_switch_ctrl),
        .rx_mixer_en     (rx_mixer_en),
        .tx_mixer_en     (tx_mixer_en),
        .adar_tx_load_1  (adar_tx_load_1),
        .adar_rx_load_1  (adar_rx_load_1),
        .adar_tx_load_2  (adar_tx_load_2),
        .adar_rx_load_2  (adar_rx_load_2),
        .adar_tx_load_3  (adar_tx_load_3),
        .adar_rx_load_3  (adar_rx_load_3),
        .adar_tx_load_4  (adar_tx_load_4),
        .adar_rx_load_4  (adar_rx_load_4),
        .adar_tr_1       (adar_tr_1),
        .adar_tr_2       (adar_tr_2),
        .adar_tr_3       (adar_tr_3),
        .adar_tr_4       (adar_tr_4),
        .chirp_counter   (chirp_counter),
        .elevation_counter (elevation_counter),
        .azimuth_counter (azimuth_counter),
        .chirp_reject_toggle (chirp_reject_toggle)
    );

    // 120 MHz DAC clock and 100 MHz system clock
    always #4.166 clk_120m = ~clk_120m;
    always #5     clk_100m = ~clk_100m;

    // Sample on the negedge: both signals are registered on the posedge.
    always @(negedge clk_120m) begin
        if (reset_n) begin
            if (chirp_reject_toggle !== reject_prev) begin
                reject_events = reject_events + 1;
                reject_prev   = chirp_reject_toggle;
            end
            if (new_chirp_frame !== frame_prev) begin
                if (new_chirp_frame)
                    frame_events = frame_events + 1;
                frame_prev = new_chirp_frame;
            end
        end
    end

    task tick(input integer n);
        integer i;
        begin
            for (i = 0; i < n; i = i + 1) @(posedge clk_120m);
        end
    endtask

    // One strobe: a single-cycle pulse in the clk_120m domain, the way the
    // transmitter's edge detector delivers it.
    task strobe;
        begin
            @(posedge clk_120m);
            new_chirp = 1'b1;
            @(posedge clk_120m);
            new_chirp = 1'b0;
        end
    endtask

    task check2(input integer got, input integer expected, input [255:0] tag);
        begin
            tests_total = tests_total + 1;
            if (got === expected) begin
                tests_passed = tests_passed + 1;
                $display("[PASS] case %0d: %0d", tag, got);
            end else begin
                $display("[FAIL] case %0d: got %0d, expected %0d", tag, got, expected);
            end
        end
    endtask

    integer guard;

    initial begin
        $dumpfile("tb_chirp_reject.vcd");
        $dumpvars(0, tb_chirp_reject);

        reset_n = 1'b0;
        mixers_enable = 1'b0;
        tick(10);
        reset_n = 1'b1;
        tick(4);

        // ---- 1. idle with the mixers disabled: a strobe cannot start a frame,
        //         so it counts as dropped ----------------------------------
        strobe;
        tick(4);
        check2(reject_events, 1, 1);
        check2(frame_events,  0, 2);

        // ---- 2. enable the mixers and strobe from idle: honoured, no reject --
        mixers_enable = 1'b1;
        tick(4);
        strobe;
        tick(2);
        check2(frame_events,  1, 3);       // the frame actually started
        check2(reject_events, 1, 4);       // and nothing new was dropped

        // ---- 3. a strobe mid-sequence is dropped and now observable ---------
        strobe;
        tick(4);
        check2(reject_events, 2, 5);
        check2(frame_events,  1, 6);       // it did not start a second frame

        // ---- 4. a second mid-sequence strobe is dropped as well -------------
        strobe;
        tick(4);
        check2(reject_events, 3, 7);
        check2(frame_events,  1, 8);

        // ---- 5. wait for the sequence to finish, then a strobe is honoured --
        guard = 0;
        while (!chirp_done && guard < 2000) begin
            tick(1);
            guard = guard + 1;
        end
        if (guard >= 2000) begin
            $display("[FAIL] sequence never completed");
            tests_total = tests_total + 1;
        end else begin
            $display("        (sequence completed after %0d cycles)", guard);
        end
        tick(8);                            // DONE -> IDLE
        strobe;
        tick(4);
        check2(frame_events,  2, 9);
        check2(reject_events, 3, 10);

        $display("=== Results: %0d/%0d passed ===", tests_passed, tests_total);
        if (tests_passed != tests_total)
            $display("*** FAILED ***");
        $finish;
    end

endmodule
