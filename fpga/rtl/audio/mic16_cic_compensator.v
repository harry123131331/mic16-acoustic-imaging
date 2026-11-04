/* 低速CIC通带下垂补偿器，采用H(z)=(-1+8z^-1-z^-2)/6。直流增益约1，轻度提升高频以补偿抽取下垂；分子、移位加法除6及饱和分别加寄存器，以适应100MHz参考时钟，不使用DSP乘法器。 */

module mic16_cic_compensator #(
    parameter integer SAMPLE_WIDTH = 48,
    parameter integer ENABLE_COMP  = 1
)(
    input  wire                         clk,
    input  wire                         rst_n,
    input  wire                         enable,

    input  wire signed [SAMPLE_WIDTH-1:0] in_sample,
    input  wire                         in_valid,

    output reg signed [SAMPLE_WIDTH-1:0] out_sample,
    output reg                          out_valid
);

    localparam integer WORK_WIDTH = SAMPLE_WIDTH + 4;

    reg signed [SAMPLE_WIDTH-1:0] z1;
    reg signed [SAMPLE_WIDTH-1:0] z2;

    reg signed [WORK_WIDTH-1:0] comp_sum;
    reg signed [WORK_WIDTH-1:0] comp_scaled;
    reg                         comp_sum_valid;
    reg                         comp_scaled_valid;

    localparam signed [SAMPLE_WIDTH-1:0] SAT_MAX =
        {1'b0, {(SAMPLE_WIDTH-1){1'b1}}};
    localparam signed [SAMPLE_WIDTH-1:0] SAT_MIN =
        {1'b1, {(SAMPLE_WIDTH-1){1'b0}}};

    /* 比较两侧必须显式有符号。Verilog原始拼接为无符号，直接比较会使多数负样本饱和至最大、正样本饱和至最小。 */
    localparam signed [WORK_WIDTH-1:0] SAT_MAX_EXT =
        {{(WORK_WIDTH-SAMPLE_WIDTH){1'b0}}, SAT_MAX};
    localparam signed [WORK_WIDTH-1:0] SAT_MIN_EXT =
        {{(WORK_WIDTH-SAMPLE_WIDTH){1'b1}}, SAT_MIN};

    function signed [WORK_WIDTH-1:0] div6_shiftadd;
        input signed [WORK_WIDTH-1:0] x;
        begin
            /* 1/6约等于1/8+1/32+1/128+1/512+1/2048+1/8192，误差低于0.03%，用于轻量补偿。 */
            div6_shiftadd = (x >>> 3)  +
                            (x >>> 5)  +
                            (x >>> 7)  +
                            (x >>> 9)  +
                            (x >>> 11) +
                            (x >>> 13);
        end
    endfunction

    function signed [SAMPLE_WIDTH-1:0] sat_sample;
        input signed [WORK_WIDTH-1:0] x;
        begin
            if ($signed(x) > SAT_MAX_EXT)
                sat_sample = SAT_MAX;
            else if ($signed(x) < SAT_MIN_EXT)
                sat_sample = SAT_MIN;
            else
                sat_sample = x[SAMPLE_WIDTH-1:0];
        end
    endfunction

    // 使用同步复位，避免逐通道复制的数据通路占用
    // XC7Z020稀缺的异步触发器控制集网络。
    always @(posedge clk) begin
        if (!rst_n) begin
            z1         <= {SAMPLE_WIDTH{1'b0}};
            z2         <= {SAMPLE_WIDTH{1'b0}};
            comp_sum   <= {WORK_WIDTH{1'b0}};
            comp_scaled <= {WORK_WIDTH{1'b0}};
            comp_sum_valid <= 1'b0;
            comp_scaled_valid <= 1'b0;
            out_sample <= {SAMPLE_WIDTH{1'b0}};
            out_valid  <= 1'b0;
        end else begin
            comp_sum_valid    <= 1'b0;
            comp_scaled_valid <= comp_sum_valid;
            out_valid         <= comp_scaled_valid;

            if (!enable) begin
                z1         <= {SAMPLE_WIDTH{1'b0}};
                z2         <= {SAMPLE_WIDTH{1'b0}};
                comp_sum   <= {WORK_WIDTH{1'b0}};
                comp_scaled <= {WORK_WIDTH{1'b0}};
                comp_sum_valid <= 1'b0;
                comp_scaled_valid <= 1'b0;
                out_sample <= {SAMPLE_WIDTH{1'b0}};
                out_valid  <= 1'b0;
            end else begin
                /* 第1级：补偿分子与样本历史。 */
                if (in_valid) begin
                    if (ENABLE_COMP != 0) begin
                        comp_sum <=
                        ({{4{z1[SAMPLE_WIDTH-1]}}, z1} <<< 3) -
                        {{4{in_sample[SAMPLE_WIDTH-1]}}, in_sample} -
                        {{4{z2[SAMPLE_WIDTH-1]}}, z2};
                    end else begin
                        comp_sum <= {{(WORK_WIDTH-SAMPLE_WIDTH){in_sample[SAMPLE_WIDTH-1]}}, in_sample};
                    end

                    z2 <= z1;
                    z1 <= in_sample;
                    comp_sum_valid <= 1'b1;
                end

                /* 第2级：移位加法近似除6。 */
                if (comp_sum_valid) begin
                    if (ENABLE_COMP != 0)
                        comp_scaled <= div6_shiftadd(comp_sum);
                    else
                        comp_scaled <= comp_sum;
                end

                /* 第3级：有符号饱和及有效脉冲。 */
                if (comp_scaled_valid)
                    out_sample <= sat_sample(comp_scaled);
            end
        end
    end

endmodule
