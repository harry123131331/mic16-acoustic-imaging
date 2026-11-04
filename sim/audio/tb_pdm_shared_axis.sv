/* 实际PDM时钟、16通道、抽取、FIFO及AXIS整链对照；只比较成功握手的真实数据。 */
`define PDM_INPUTS \
    .pdm_data0(bits[0]),.pdm_data1(bits[1]),.pdm_data2(bits[2]),.pdm_data3(bits[3]), \
    .pdm_data4(bits[4]),.pdm_data5(bits[5]),.pdm_data6(bits[6]),.pdm_data7(bits[7]), \
    .pdm_data8(bits[8]),.pdm_data9(bits[9]),.pdm_data10(bits[10]),.pdm_data11(bits[11]), \
    .pdm_data12(bits[12]),.pdm_data13(bits[13]),.pdm_data14(bits[14]),.pdm_data15(bits[15])
module tb_pdm_shared_axis;
    reg clk=0;always #5 clk=~clk;
    reg rst_n=0,enable=0,ready=0;
    reg[15:0] bits=0;
    wire[31:0] old_data,new_data,old_errors,new_errors;
    wire[3:0] old_keep,new_keep;
    wire old_last,new_last,old_valid,new_valid,old_clk,new_clk,old_overflow,new_overflow;
    pl_pdm_top_16 #(.SHARED_DECIMATOR(0)) reference (
        .clk_ref(clk),.rst_n(rst_n),.enable(enable),.pdm_clk_div(16'd20),`PDM_INPUTS,
        .pdm_clk_out(old_clk),.m_axis_tdata(old_data),.m_axis_tkeep(old_keep),.m_axis_tlast(old_last),
        .m_axis_tvalid(old_valid),.m_axis_tready(ready),.overflow_error(old_overflow),.overflow_count(old_errors),
        .dbg_channel(),.dbg_sample_index(),.dbg_fifo_level(),.pdm_clk_running());
    pl_pdm_top_16 #(.SHARED_DECIMATOR(1)) dut (
        .clk_ref(clk),.rst_n(rst_n),.enable(enable),.pdm_clk_div(16'd20),`PDM_INPUTS,
        .pdm_clk_out(new_clk),.m_axis_tdata(new_data),.m_axis_tkeep(new_keep),.m_axis_tlast(new_last),
        .m_axis_tvalid(new_valid),.m_axis_tready(ready),.overflow_error(new_overflow),.overflow_count(new_errors),
        .dbg_channel(),.dbg_sample_index(),.dbg_fifo_level(),.pdm_clk_running());
    reg[36:0] queue[0:2047];
    reg[36:0] old_hold,new_hold;
    reg old_stalled=0,new_stalled=0;
    integer written=0,read=0,packets=0,cycles=0;
    always @(posedge clk)begin
        cycles=cycles+1;
        if(!rst_n || !enable)begin
            written=0;read=0;packets=0;old_stalled=0;new_stalled=0;
        end else begin
            if(old_clk!==new_clk)$fatal(1,"共享通路改变PDM时钟");
            if(old_overflow || new_overflow || old_errors || new_errors)$fatal(1,"正常背压产生采集错误");
            if(old_stalled && (!old_valid || {old_last,old_keep,old_data}!==old_hold))$fatal(1,"旧AXIS未保持阻塞数据");
            if(new_stalled && (!new_valid || {new_last,new_keep,new_data}!==new_hold))$fatal(1,"新AXIS未保持阻塞数据");
            old_hold={old_last,old_keep,old_data};new_hold={new_last,new_keep,new_data};
            old_stalled=old_valid&&!ready;new_stalled=new_valid&&!ready;
            if(old_valid && ready)begin
                queue[written%2048]={old_last,old_keep,old_data};written=written+1;
            end
            if(written-read>2047)$fatal(1,"AXIS参考队列满");
            if(new_valid && ready)begin
                if(read==written || {new_last,new_keep,new_data}!==queue[read%2048])
                    $fatal(1,"AXIS样本/通道/包边界不一致 word=%0d",read);
                if(new_keep!=4'hf || new_last!=(read%256==255))$fatal(1,"不是原1024字节包");
                read=read+1;if(new_last)packets=packets+1;
                if(packets==5)begin
                    $display("PDM_SHARED_AXIS_PASS words=%0d packets=%0d PDM_clock_unchanged=1 backpressure_checked=1",read,packets);
                    $finish;
                end
            end
        end
    end
    reg[31:0] rng=32'h23e59ab1;
    initial begin
        repeat(8)@(negedge clk);rst_n=1;enable=1;
        forever begin
            @(negedge clk);
            rng={rng[30:0],rng[31]^rng[21]^rng[1]^rng[0]};bits=rng[15:0];
            ready=(cycles%10000<9700) && (rng[0]||rng[7]);
        end
    end
    initial begin #3000000;$fatal(1,"AXIS仿真超时");end
endmodule
`undef PDM_INPUTS
