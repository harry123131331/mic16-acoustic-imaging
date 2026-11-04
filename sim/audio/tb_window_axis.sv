/* 直接核验现有打包器的整窗TLAST；滤波数学已另行逐位核验。 */
`timescale 1ns/1ps
module tb_window_axis;
    reg clk=0;always #5 clk=~clk;
    reg rst_n=0,enable=0,ready=0;
    reg [511:0] samples=0;
    reg [15:0] valids=0;
    wire [31:0] data,errors;
    wire [3:0] keep;
    wire valid,last,overflow;
    wire [15:0] level;
    wire [31:0] pack_data;
    wire [3:0] pack_keep;
    wire pack_last,pack_valid,pack_ready;
    pdm_axis_pack_nch #(.SAMPLES_PER_PACKET(40960),.FIFO_DEPTH(8)) dut(
        .clk(clk),.rst_n(rst_n),.enable(enable),.pcm_samples_flat(samples),.pcm_valids(valids),
        .m_axis_tdata(pack_data),.m_axis_tkeep(pack_keep),.m_axis_tlast(pack_last),.m_axis_tvalid(pack_valid),
        .m_axis_tready(pack_ready),.dbg_channel(),.dbg_sample_index(),.overflow_error(overflow),
        .dbg_fifo_level(level),.overflow_count(errors));
`ifdef M16_VENDOR_FIFO
    /* 直接使用当前BD生成的真实厂商IP模型；enable只复位音频FIFO。 */
    bd_mic16_dataflow_axis_data_fifo_0_0 fifo(
        .s_axis_aresetn(rst_n && enable),.s_axis_aclk(clk),
        .s_axis_tvalid(pack_valid),.s_axis_tready(pack_ready),.s_axis_tdata(pack_data),
        .s_axis_tkeep(pack_keep),.s_axis_tlast(pack_last),
        .m_axis_tvalid(valid),.m_axis_tready(ready),.m_axis_tdata(data),
        .m_axis_tkeep(keep),.m_axis_tlast(last));
`else
    assign data=pack_data;assign keep=pack_keep;assign last=pack_last;
    assign valid=pack_valid;assign pack_ready=ready;
`endif
    integer sent=0,got=0,packets=0,total=0,cycle=0,i,phase=0,target=0;
    integer fifo_ingress=0;
    reg stalled=0;
    reg [36:0] hold_word;
    reg [31:0] rng=32'hc9518327;
    function [31:0] value;
        input integer s,c;
        begin value=32'h81234000 ^ (s*32'h1e35a7bd) ^ (c*32'h73f17495);end
    endfunction
    always @(posedge clk)begin
        if(rst_n && enable)begin
            if(pack_valid && pack_ready && !ready)fifo_ingress=fifo_ingress+1;
            if(phase<2 && (overflow || errors))$fatal(1,"正常背压发生丢样");
            if(stalled && (!valid || {last,keep,data}!==hold_word))$fatal(1,"阻塞时AXIS数据变化");
            if(valid && ready && phase<2)begin
                if(data!==value(got/16,got%16) || keep!==4'hf)$fatal(1,"通道或样本不一致 word=%0d",got);
                if(last!==(got%655360==655359))$fatal(1,"整窗TLAST位置错误 word=%0d",got);
                got=got+1;total=total+1;
                if(last)packets=packets+1;
            end
            hold_word={last,keep,data};stalled=valid&&!ready;
        end else stalled=0;
    end
    task reset_source;
        begin
            @(negedge clk);enable=0;valids=0;ready=0;repeat(32)@(negedge clk);
            sent=0;got=0;cycle=0;enable=1;
        end
    endtask
    task run_windows;
        input integer count;
        begin
            target=count*40960;
            while(got<count*655360)begin
                @(negedge clk);cycle=cycle+1;
                rng={rng[30:0],rng[31]^rng[21]^rng[1]^rng[0]};
                ready=(rng[0]||rng[4]||rng[7]) && cycle%30000<29744;
                valids=0;
                if(cycle%64==0 && sent<target)begin
                    for(i=0;i<16;i=i+1)samples[32*i+:32]=value(sent,i);
                    valids=16'hffff;sent=sent+1;
                end
            end
            if(sent!=target)$fatal(1,"未发送完整窗");
        end
    endtask
    initial begin
        repeat(5)@(negedge clk);rst_n=1;reset_source();
        run_windows(2);
        /* 停止时丢弃未完成尾窗；重启后首样本/通道/TLAST都须重新对齐。 */
        fifo_ingress=0;
`ifdef M16_VENDOR_FIFO
        repeat(2048)begin
            @(negedge clk);valids=0;ready=0;
`else
        repeat(173)begin
            @(negedge clk);valids=0;ready=1;
`endif
            if(cycle%64==0)begin
                for(i=0;i<16;i=i+1)samples[32*i+:32]=value(sent,i);
                valids=16'hffff;sent=sent+1;
            end
            cycle=cycle+1;
        end
`ifdef M16_VENDOR_FIFO
        if(fifo_ingress<256)$fatal(1,"未在真实FIFO中预留旧窗尾部，不能证明停启清除");
`endif
        phase=1;reset_source();run_windows(1);
        /* 故意超过FIFO容量，必须锁存错误；停启才清除。 */
        phase=2;ready=0;valids=0;
        repeat(96)begin
            @(negedge clk);valids=16'hffff;
            @(negedge clk);valids=0;repeat(63)@(negedge clk);
        end
        if(!overflow || !errors)$fatal(1,"FIFO满未报告溢出");
        reset_source();@(negedge clk);
        if(overflow || errors || level)$fatal(1,"重启未清除旧采集状态");
        $display("WINDOW_AXIS_PASS packets=%0d words=%0d backpressure=1 restart=1 overflow=1",packets,total);
        $finish;
    end
    initial begin #200000000;$fatal(1,"整窗AXIS仿真超时");end
endmodule
