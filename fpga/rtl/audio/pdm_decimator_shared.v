/* 16路共享抽取通路。同步PDM位先整组锁存，再每拍处理一路；每路独立状态保存在小RAM。
 * CIC、直流阻断、补偿和后置FIR保持原位宽、截断、加法次序与饱和。只增加固定传输延迟。
 * 采样tick间隔必须至少17拍，现有时钟发生器最小32拍。超期锁存错误，禁止冒充正常采集。
 * 原16路并行实现保留为黄金参考，本模块尚须整机布线和实板验收。 */
module pdm_decimator_shared #(
    parameter integer DECIMATION = 64,
    parameter integer SCALE_SHIFT = 12,
    parameter integer ACC_WIDTH = 32,
    parameter integer ENABLE_DC_BLOCK = 1,
    parameter integer DC_BLOCK_SHIFT = 10,
    parameter integer ENABLE_CIC_COMP = 1,
    parameter integer ENABLE_POST_FIR = 1
)(
    input wire clk_ref, rst_n, enable,
    input wire pdm_sample_tick,
    input wire [15:0] pdm_data,
    output reg [511:0] pcm_samples_flat,
    output reg pcm_valid,
    output reg schedule_error,
    output reg [31:0] schedule_error_count
);
    function integer clog2;
        input integer x;
        begin x=x-1; for(clog2=0;x>0;clog2=clog2+1)x=x>>1; end
    endfunction
    localparam integer W=ACC_WIDTH;
    localparam integer F=W+4;
    localparam integer NET_SHIFT=SCALE_SHIFT-2*clog2(DECIMATION);
    localparam signed [W-1:0] ONE={{(W-1){1'b0}},1'b1};
    localparam signed [W-1:0] MAX32={{(W-32){1'b0}},32'h7fffffff};
    localparam signed [W-1:0] MIN32={{(W-32){1'b1}},32'h80000000};
    localparam signed [F-1:0] MAXW={4'b0000,1'b0,{(W-1){1'b1}}};
    localparam signed [F-1:0] MINW={4'b1111,1'b1,{(W-1){1'b0}}};

    /* 只复位有效位，不逐字清RAM。首次访问的旧状态显式视为0，不依赖上电内容。 */
    (* ram_style="distributed" *) reg signed [W-1:0] i1[0:15],i2[0:15],i3[0:15];
    (* ram_style="distributed" *) reg signed [W-1:0] d1[0:15],d2[0:15],d3[0:15],dc[0:15];
    (* ram_style="distributed" *) reg signed [W-1:0] z1[0:15],z2[0:15];
    (* ram_style="distributed" *) reg signed [W-1:0] f1[0:15],f2[0:15],f3[0:15],f4[0:15];
    reg [15:0] cic_seen,low_seen;
    reg [15:0] pdm_meta,pdm_sync,group_bits;
    reg [15:0] decim_count;
    reg dispatch,group_decim;
    reg [3:0] channel;
    reg cv0,cv1,cd0,cd1,cf0,cf1;
    reg [3:0] ct0,ct1;
    reg signed [W-1:0] a1,a2,prior2,prior3a,prior3b;
    wire signed [W-1:0] pdm_value=group_bits[channel]?ONE:-ONE;
    wire signed [W-1:0] next1=(cic_seen[channel]?i1[channel]:{W{1'b0}})+pdm_value;
    wire signed [W-1:0] next2=prior2+a1;
    wire signed [W-1:0] next3=prior3b+a2;

    reg [11:0] valid,fresh;
    reg [3:0] tag[0:11];
    reg signed [W-1:0] s0,s1,s2,s3,s4,s5,delta5,s6,comp9,pass10,pre11;
    reg signed [F-1:0] comp7,comp8,p0,p1,p2;
    reg signed [W-1:0] dc_next,dc_update;
    reg signed [F-1:0] fir_sum;
    integer k;

    function signed [F-1:0] widen;
        input signed [W-1:0] x;
        begin widen={{4{x[W-1]}},x}; end
    endfunction
    function signed [F-1:0] div6;
        input signed [F-1:0] x;
        begin div6=(x>>>3)+(x>>>5)+(x>>>7)+(x>>>9)+(x>>>11)+(x>>>13); end
    endfunction
    function signed [W-1:0] satw;
        input signed [F-1:0] x;
        begin
            if(x>MAXW)satw={1'b0,{(W-1){1'b1}}};
            else if(x<MINW)satw={1'b1,{(W-1){1'b0}}};
            else satw=x[W-1:0];
        end
    endfunction
    function signed [31:0] sat32;
        input signed [W-1:0] x;
        begin
            if(x>MAX32)sat32=32'h7fffffff;
            else if(x<MIN32)sat32=32'h80000000;
            else sat32=x[31:0];
        end
    endfunction

    /* 与原输入同步器完全一致；采样时只锁存已经同步的旧值。 */
    always @(posedge clk_ref) begin
        if(!rst_n)begin pdm_meta<=0;pdm_sync<=0;end
        else begin pdm_meta<=pdm_data;pdm_sync<=pdm_meta;end
    end

    always @(posedge clk_ref) begin
        if(!rst_n || !enable)begin
            cic_seen<=0;low_seen<=0;decim_count<=0;dispatch<=0;group_decim<=0;channel<=0;
            cv0<=0;cv1<=0;cd0<=0;cd1<=0;cf0<=0;cf1<=0;
            valid<=0;fresh<=0;pcm_samples_flat<=0;pcm_valid<=0;
            schedule_error<=0;schedule_error_count<=0;
        end else begin
            pcm_valid<=0;
            if(pdm_sample_tick)begin
                if(dispatch)begin
                    schedule_error<=1;
                    if(schedule_error_count!=32'hffffffff)schedule_error_count<=schedule_error_count+1;
                end else begin
                    group_bits<=pdm_sync;dispatch<=1;channel<=0;
                    group_decim<=(decim_count==DECIMATION-1);
                    if(decim_count==DECIMATION-1)decim_count<=0;
                    else decim_count<=decim_count+1;
                end
            end
            cv0<=dispatch;cv1<=cv0;
            cd0<=group_decim;cd1<=cd0;
            cf0<=!low_seen[channel];cf1<=cf0;
            ct0<=channel;ct1<=ct0;
            if(dispatch)begin
                i1[channel]<=next1;a1<=next1;
                prior2<=cic_seen[channel]?i2[channel]:{W{1'b0}};
                prior3a<=cic_seen[channel]?i3[channel]:{W{1'b0}};
                cic_seen[channel]<=1;
                if(group_decim)low_seen[channel]<=1;
                if(channel==15)dispatch<=0;else channel<=channel+1;
            end
            if(cv0)begin i2[ct0]<=next2;a2<=next2;prior3b<=prior3a;end
            if(cv1)begin i3[ct1]<=next3;s0<=next3;end

            valid[0]<=cv1&&cd1;tag[0]<=ct1;fresh[0]<=cf1;
            for(k=1;k<12;k=k+1)begin
                valid[k]<=valid[k-1];tag[k]<=tag[k-1];fresh[k]<=fresh[k-1];
            end
            if(valid[0])begin s1<=s0-(fresh[0]?{W{1'b0}}:d1[tag[0]]);d1[tag[0]]<=s0;end
            if(valid[1])begin s2<=s1-(fresh[1]?{W{1'b0}}:d2[tag[1]]);d2[tag[1]]<=s1;end
            if(valid[2])begin s3<=s2-(fresh[2]?{W{1'b0}}:d3[tag[2]]);d3[tag[2]]<=s2;end
            if(valid[3])begin
                if(NET_SHIFT>=0)s4<=s3<<<NET_SHIFT;else s4<=s3>>>(-NET_SHIFT);
            end
            if(valid[4])begin
                s5<=s4;
                if(ENABLE_DC_BLOCK!=0)delta5<=s4-(fresh[4]?{W{1'b0}}:dc[tag[4]]);
                else delta5<=0;
            end
            if(valid[5])begin
                if(ENABLE_DC_BLOCK!=0)begin
                    dc_update=delta5>>>DC_BLOCK_SHIFT;
                    dc_next=(fresh[5]?{W{1'b0}}:dc[tag[5]])+dc_update;
                    dc[tag[5]]<=dc_next;s6<=s5-dc_next;
                end else begin dc[tag[5]]<=0;s6<=s5;end
            end
            if(valid[6])begin
                if(ENABLE_CIC_COMP!=0)
                    comp7<=(widen(fresh[6]?{W{1'b0}}:z1[tag[6]])<<<3)-widen(s6)-widen(fresh[6]?{W{1'b0}}:z2[tag[6]]);
                else comp7<=widen(s6);
                z2[tag[6]]<=fresh[6]?{W{1'b0}}:z1[tag[6]];z1[tag[6]]<=s6;
            end
            if(valid[7])begin
                if(ENABLE_CIC_COMP!=0)comp8<=div6(comp7);else comp8<=comp7;
            end
            if(valid[8])comp9<=satw(comp8);
            if(valid[9])begin
                if(ENABLE_POST_FIR!=0)begin
                    p0<=widen(comp9)+(widen(fresh[9]?{W{1'b0}}:f1[tag[9]])<<<2);
                    p1<=((widen(fresh[9]?{W{1'b0}}:f2[tag[9]])<<<2)+
                         (widen(fresh[9]?{W{1'b0}}:f2[tag[9]])<<<1))+
                         (widen(fresh[9]?{W{1'b0}}:f3[tag[9]])<<<2);
                    p2<=widen(fresh[9]?{W{1'b0}}:f4[tag[9]]);
                    f4[tag[9]]<=fresh[9]?{W{1'b0}}:f3[tag[9]];
                    f3[tag[9]]<=fresh[9]?{W{1'b0}}:f2[tag[9]];
                    f2[tag[9]]<=fresh[9]?{W{1'b0}}:f1[tag[9]];f1[tag[9]]<=comp9;
                end else begin p0<=0;p1<=0;p2<=0;f1[tag[9]]<=0;f2[tag[9]]<=0;f3[tag[9]]<=0;f4[tag[9]]<=0;end
                pass10<=comp9;
            end
            if(valid[10])begin
                if(ENABLE_POST_FIR!=0)begin fir_sum=p0+p1+p2;pre11<=fir_sum>>>4;end
                else pre11<=pass10;
            end
            /* 每路在同一标签下完成饱和，最后一路写入后整组同时交给AXIS打包器。 */
            if(valid[11])begin
                pcm_samples_flat[tag[11]*32+:32]<=sat32(pre11);
                if(tag[11]==15)pcm_valid<=1;
            end
        end
    end
endmodule
