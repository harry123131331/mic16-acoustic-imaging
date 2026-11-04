/* 真实旧16路RTL与共享通路逐样本对照。允许固定延迟变化，不允许跳样本或通道串扰。 */
module shared_case #(
    parameter integer ID=0,D=64,W=32,SHIFT=12,DC=1,COMP=1,FIR=1
)(input wire clk,rst_n,enable,tick,input wire[15:0] bits,
  output reg[31:0] compared=0,output wire fault);
    wire[511:0] original,actual;
    wire[15:0] ov;
    wire av;
    wire[31:0] fault_count;
    genvar c;
    generate for(c=0;c<16;c=c+1)begin:REF
        pdm_decimator #(.DECIMATION(D),.SCALE_SHIFT(SHIFT),.ACC_WIDTH(W),
            .ENABLE_DC_BLOCK(DC),.ENABLE_CIC_COMP(COMP),.ENABLE_POST_FIR(FIR)) reference (
            .clk_ref(clk),.rst_n(rst_n),.enable(enable),.pdm_sample_tick(tick),.pdm_data(bits[c]),
            .pcm_sample(original[c*32+:32]),.pcm_valid(ov[c]));
    end endgenerate
    pdm_decimator_shared #(.DECIMATION(D),.SCALE_SHIFT(SHIFT),.ACC_WIDTH(W),
        .ENABLE_DC_BLOCK(DC),.ENABLE_CIC_COMP(COMP),.ENABLE_POST_FIR(FIR)) dut (
        .clk_ref(clk),.rst_n(rst_n),.enable(enable),.pdm_sample_tick(tick),.pdm_data(bits),
        .pcm_samples_flat(actual),.pcm_valid(av),.schedule_error(fault),.schedule_error_count(fault_count));
    reg[511:0] queue[0:15];
    integer written=0,read=0,j;
    always @(posedge clk)begin
        if(!rst_n || !enable)begin written=0;read=0;end
        else begin
            if(fault || fault_count)$fatal(1,"正常节拍不应超期 case=%0d",ID);
            if(ov!=0 && ov!=16'hffff)$fatal(1,"旧通道有效位不一致");
            if(&ov)begin queue[written%16]=original;written=written+1;end
            if(written-read>15)$fatal(1,"参考队列满");
            if(av)begin
                if(read==written)$fatal(1,"共享通路产生额外样本 case=%0d",ID);
                for(j=0;j<16;j=j+1)begin
                    if(actual[j*32+:32]!==queue[read%16][j*32+:32])
                        $fatal(1,"PCM不一致 case=%0d group=%0d ch=%0d actual=%h expected=%h",ID,read,j,
                            actual[j*32+:32],queue[read%16][j*32+:32]);
                end
                read=read+1;compared=compared+16;
            end
        end
    end
endmodule

module tb_pdm_decimator_shared;
    reg clk=0;always #5 clk=~clk;
    reg rst_n=0,enable=0,tick=0;
    reg[15:0] bits=0;
    wire[31:0] n0,n1,n2,n3;
    wire[3:0] fault;
    shared_case #(.ID(0)) a(clk,rst_n,enable,tick,bits,n0,fault[0]);
    shared_case #(.ID(1),.D(1),.SHIFT(12),.DC(0),.COMP(0),.FIR(0)) b(clk,rst_n,enable,tick,bits,n1,fault[1]);
    shared_case #(.ID(2),.D(8),.W(48),.SHIFT(0),.DC(1),.COMP(0),.FIR(1)) c(clk,rst_n,enable,tick,bits,n2,fault[2]);
    shared_case #(.ID(3),.D(64),.W(32),.SHIFT(12),.DC(0),.COMP(1),.FIR(0)) d(clk,rst_n,enable,tick,bits,n3,fault[3]);
    reg bad_enable=0,bad_tick=0;
    wire deadline_fault;
    wire[31:0] deadline_count;
    pdm_decimator_shared deadline (.clk_ref(clk),.rst_n(rst_n),.enable(bad_enable),
        .pdm_sample_tick(bad_tick),.pdm_data(16'h5555),.pcm_samples_flat(),.pcm_valid(),
        .schedule_error(deadline_fault),.schedule_error_count(deadline_count));
    integer i,j,countdown;
    reg[31:0] random_state=32'h7b5e9d31;
    task phase;
        input integer cycles,pattern,period;
        begin
            countdown=0;
            for(i=0;i<cycles;i=i+1)begin
                @(negedge clk);
                tick=(countdown==0);
                if(tick)countdown=period-1;else countdown=countdown-1;
                random_state={random_state[30:0],random_state[31]^random_state[21]^random_state[1]^random_state[0]};
                case(pattern)
                    0:bits=16'hffff;
                    1:bits=16'h0000;
                    2:bits=i[0]?16'haaaa:16'h5555;
                    default:bits=random_state[15:0];
                endcase
            end
        end
    endtask
    task drain;
        begin @(negedge clk);tick=0;repeat(80)@(negedge clk);
            if(a.read!=a.written || b.read!=b.written || c.read!=c.written || d.read!=d.written)
                $fatal(1,"排空后仍缺少PCM样本");
        end
    endtask
    initial begin
        repeat(6)@(negedge clk);rst_n=1;enable=1;
        phase(70000,0,32);drain();
        phase(70000,1,40);drain();
        phase(70000,2,64);drain();
        phase(70000,3,32);drain();
        /* 在积分和低速流水可能未排空时反复停启，验证RAM旧状态被有效位隔离。 */
        for(j=0;j<7;j=j+1)begin
            @(negedge clk);enable=0;tick=0;repeat(3)@(negedge clk);enable=1;
            phase(2600+j*23,3,32);
        end
        drain();
        if(n0<1000 || n1<10000 || n2<1000 || n3<1000)$fatal(1,"对照覆盖不足");
        @(negedge clk);enable=0;tick=0;bad_enable=1;
        repeat(10)begin bad_tick=1;@(negedge clk);bad_tick=0;repeat(3)@(negedge clk);end
        if(!deadline_fault || deadline_count==0)$fatal(1,"未锁存超期错误");
        bad_enable=0;bad_tick=0;repeat(3)@(negedge clk);
        if(deadline_fault || deadline_count)$fatal(1,"停止后错误未复位");
        $display("PDM_SHARED_BITEXACT_PASS samples=%0d cases=4 deadline_errors_checked=1",n0+n1+n2+n3);
        $finish;
    end
    initial begin #10000000;$fatal(1,"仿真超时");end
endmodule
