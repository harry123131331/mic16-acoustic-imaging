/* 单路PDM转PCM的流水线抽取器。将低速CIC梳状、直流阻断、后置FIR及饱和拆成寄存器阶段，改善16路复制时的100MHz时序。流程：PDM映射±1→CIC3积分→DECIMATION抽取→三级梳状→幅值修正→可选直流阻断→可选CIC通带补偿→可选五抽头FIR[1,4,6,4,1]/16→有符号32位饱和。 */

module pdm_decimator #(
    parameter integer DECIMATION      = 64,
    parameter integer SCALE_SHIFT     = 12,
    parameter integer ACC_WIDTH       = 48,
    parameter integer ENABLE_DC_BLOCK = 1,
    parameter integer DC_BLOCK_SHIFT  = 10,
    parameter integer ENABLE_CIC_COMP = 1,
    parameter integer ENABLE_POST_FIR = 1
)(
    input  wire               clk_ref,
    input  wire               rst_n,
    input  wire               enable,

    input  wire               pdm_sample_tick,
    input  wire               pdm_data,

    output reg signed [31:0]  pcm_sample,
    output reg                pcm_valid
);

    function integer clog2;
        input integer value;
        integer i;
        begin
            value = value - 1;
            for (i = 0; value > 0; i = i + 1)
                value = value >> 1;
            clog2 = i;
        end
    endfunction

    localparam integer LOG2_DECIMATION = clog2(DECIMATION);
    localparam integer SCALE_CORRECT   = 2 * LOG2_DECIMATION;
    localparam integer NET_SHIFT       = SCALE_SHIFT - SCALE_CORRECT;
    localparam integer FIR_WIDTH       = ACC_WIDTH + 4;

    localparam signed [ACC_WIDTH-1:0] PDM_POS = {{(ACC_WIDTH-1){1'b0}}, 1'b1};
    localparam signed [ACC_WIDTH-1:0] PDM_NEG = -PDM_POS;

    localparam signed [ACC_WIDTH-1:0] SAT32_MAX =
        {{(ACC_WIDTH-32){1'b0}}, 32'h7fffffff};
    localparam signed [ACC_WIDTH-1:0] SAT32_MIN =
        {{(ACC_WIDTH-32){1'b1}}, 32'h80000000};

    /* 异步PDM输入的同步器。 */
    reg pdm_meta;
    reg pdm_sync;

    /* CIC3积分器。 */
    reg [15:0] decim_cnt;
    reg signed [ACC_WIDTH-1:0] int1;
    reg signed [ACC_WIDTH-1:0] int2;
    reg signed [ACC_WIDTH-1:0] int3;

    reg signed [ACC_WIDTH-1:0] pdm_value;
    reg signed [ACC_WIDTH-1:0] int1_next;
    reg signed [ACC_WIDTH-1:0] int2_next;
    reg signed [ACC_WIDTH-1:0] int3_next;

    /* CIC3梳状延迟状态。 */
    reg signed [ACC_WIDTH-1:0] comb_delay1;
    reg signed [ACC_WIDTH-1:0] comb_delay2;
    reg signed [ACC_WIDTH-1:0] comb_delay3;

    /* 流水线阶段。 */
    reg                         v0_decim;
    reg signed [ACC_WIDTH-1:0]  s0_int3;

    reg                         v1_comb1;
    reg signed [ACC_WIDTH-1:0]  s1_comb1;

    reg                         v2_comb2;
    reg signed [ACC_WIDTH-1:0]  s2_comb2;

    reg                         v3_comb3;
    reg signed [ACC_WIDTH-1:0]  s3_comb3;

    reg                         v4_scaled;
    reg signed [ACC_WIDTH-1:0]  s4_scaled;

    reg                         v5_dc_diff;
    reg signed [ACC_WIDTH-1:0]  s5_scaled;
    reg signed [ACC_WIDTH-1:0]  s5_dc_diff;

    reg                         v6_dc_out;
    reg signed [ACC_WIDTH-1:0]  s6_dc_removed;
    reg signed [ACC_WIDTH-1:0]  dc_est;

    reg signed [ACC_WIDTH-1:0]  dc_update;
    reg signed [ACC_WIDTH-1:0]  dc_est_next;

    /* 可选低速CIC补偿级。 */
    wire                        comp_valid;
    wire signed [ACC_WIDTH-1:0] comp_sample;

    /* 五抽头后置FIR状态及流水线。 */
    reg signed [ACC_WIDTH-1:0]  fir_z1;
    reg signed [ACC_WIDTH-1:0]  fir_z2;
    reg signed [ACC_WIDTH-1:0]  fir_z3;
    reg signed [ACC_WIDTH-1:0]  fir_z4;

    reg                         v7_fir_part;
    reg signed [FIR_WIDTH-1:0]  s7_p0;
    reg signed [FIR_WIDTH-1:0]  s7_p1;
    reg signed [FIR_WIDTH-1:0]  s7_p2;
    reg signed [ACC_WIDTH-1:0]  s7_passthrough;

    reg                         v8_fir_out;
    reg signed [ACC_WIDTH-1:0]  s8_pre_sat;

    reg signed [FIR_WIDTH-1:0]  fir_sum_final;

    function signed [31:0] sat32;
        input signed [ACC_WIDTH-1:0] val;
        begin
            if (val > SAT32_MAX)
                sat32 = 32'sh7fffffff;
            else if (val < SAT32_MIN)
                sat32 = 32'sh80000000;
            else
                sat32 = val[31:0];
        end
    endfunction

    mic16_cic_compensator #(
        .SAMPLE_WIDTH (ACC_WIDTH),
        .ENABLE_COMP  (ENABLE_CIC_COMP)
    ) u_cic_comp (
        .clk        (clk_ref),
        .rst_n      (rst_n),
        .enable     (enable),
        .in_sample  (s6_dc_removed),
        .in_valid   (v6_dc_out),
        .out_sample (comp_sample),
        .out_valid  (comp_valid)
    );

    // 100MHz时钟域使用同步复位；7系列器件的异步复位
    // 会进入每个触发器的控制集，复制到16路后
    // 可能阻碍原本合法的逻辑单元打包。
    always @(posedge clk_ref) begin
        if (!rst_n) begin
            pdm_meta <= 1'b0;
            pdm_sync <= 1'b0;
        end else begin
            pdm_meta <= pdm_data;
            pdm_sync <= pdm_meta;
        end
    end

    always @(posedge clk_ref) begin
        if (!rst_n) begin
            decim_cnt <= 16'd0;

            int1 <= {ACC_WIDTH{1'b0}};
            int2 <= {ACC_WIDTH{1'b0}};
            int3 <= {ACC_WIDTH{1'b0}};

            comb_delay1 <= {ACC_WIDTH{1'b0}};
            comb_delay2 <= {ACC_WIDTH{1'b0}};
            comb_delay3 <= {ACC_WIDTH{1'b0}};

            v0_decim <= 1'b0;
            s0_int3  <= {ACC_WIDTH{1'b0}};

            v1_comb1 <= 1'b0;
            s1_comb1 <= {ACC_WIDTH{1'b0}};

            v2_comb2 <= 1'b0;
            s2_comb2 <= {ACC_WIDTH{1'b0}};

            v3_comb3 <= 1'b0;
            s3_comb3 <= {ACC_WIDTH{1'b0}};

            v4_scaled <= 1'b0;
            s4_scaled <= {ACC_WIDTH{1'b0}};

            v5_dc_diff <= 1'b0;
            s5_scaled  <= {ACC_WIDTH{1'b0}};
            s5_dc_diff <= {ACC_WIDTH{1'b0}};

            v6_dc_out    <= 1'b0;
            s6_dc_removed <= {ACC_WIDTH{1'b0}};
            dc_est        <= {ACC_WIDTH{1'b0}};

            fir_z1 <= {ACC_WIDTH{1'b0}};
            fir_z2 <= {ACC_WIDTH{1'b0}};
            fir_z3 <= {ACC_WIDTH{1'b0}};
            fir_z4 <= {ACC_WIDTH{1'b0}};

            v7_fir_part   <= 1'b0;
            s7_p0         <= {FIR_WIDTH{1'b0}};
            s7_p1         <= {FIR_WIDTH{1'b0}};
            s7_p2         <= {FIR_WIDTH{1'b0}};
            s7_passthrough <= {ACC_WIDTH{1'b0}};

            v8_fir_out <= 1'b0;
            s8_pre_sat <= {ACC_WIDTH{1'b0}};

            pcm_sample <= 32'sd0;
            pcm_valid  <= 1'b0;
        end else begin
            /* 各级有效信号只在前级有效时推进；末级pcm_valid为单周期脉冲。 */
            v0_decim   <= 1'b0;
            pcm_valid  <= 1'b0;

            if (!enable) begin
                decim_cnt <= 16'd0;

                int1 <= {ACC_WIDTH{1'b0}};
                int2 <= {ACC_WIDTH{1'b0}};
                int3 <= {ACC_WIDTH{1'b0}};

                comb_delay1 <= {ACC_WIDTH{1'b0}};
                comb_delay2 <= {ACC_WIDTH{1'b0}};
                comb_delay3 <= {ACC_WIDTH{1'b0}};

                v0_decim <= 1'b0;
                v1_comb1 <= 1'b0;
                v2_comb2 <= 1'b0;
                v3_comb3 <= 1'b0;
                v4_scaled <= 1'b0;
                v5_dc_diff <= 1'b0;
                v6_dc_out <= 1'b0;
                v7_fir_part <= 1'b0;
                v8_fir_out <= 1'b0;

                dc_est <= {ACC_WIDTH{1'b0}};

                fir_z1 <= {ACC_WIDTH{1'b0}};
                fir_z2 <= {ACC_WIDTH{1'b0}};
                fir_z3 <= {ACC_WIDTH{1'b0}};
                fir_z4 <= {ACC_WIDTH{1'b0}};

                pcm_sample <= 32'sd0;
                pcm_valid  <= 1'b0;
            end else begin
                /* 输入级：仅PDM采样脉冲到来时更新CIC3积分器。 */
                if (pdm_sample_tick) begin
                    pdm_value = pdm_sync ? PDM_POS : PDM_NEG;

                    int1_next = int1 + pdm_value;
                    int2_next = int2 + int1_next;
                    int3_next = int3 + int2_next;

                    int1 <= int1_next;
                    int2 <= int2_next;
                    int3 <= int3_next;

                    if (decim_cnt == DECIMATION - 1) begin
                        decim_cnt <= 16'd0;
                        s0_int3   <= int3_next;
                        v0_decim  <= 1'b1;
                    end else begin
                        decim_cnt <= decim_cnt + 16'd1;
                    end
                end

                /* 第1级：第一梳状级。 */
                v1_comb1 <= v0_decim;
                if (v0_decim) begin
                    s1_comb1    <= s0_int3 - comb_delay1;
                    comb_delay1 <= s0_int3;
                end

                /* 第2级：第二梳状级。 */
                v2_comb2 <= v1_comb1;
                if (v1_comb1) begin
                    s2_comb2    <= s1_comb1 - comb_delay2;
                    comb_delay2 <= s1_comb1;
                end

                /* 第3级：第三梳状级。 */
                v3_comb3 <= v2_comb2;
                if (v2_comb2) begin
                    s3_comb3    <= s2_comb2 - comb_delay3;
                    comb_delay3 <= s2_comb2;
                end

                /* 第4级：幅值修正。 */
                v4_scaled <= v3_comb3;
                if (v3_comb3) begin
                    if (NET_SHIFT >= 0)
                        s4_scaled <= s3_comb3 <<< NET_SHIFT;
                    else
                        s4_scaled <= s3_comb3 >>> (-NET_SHIFT);
                end

                /* 第5级：直流阻断差分。 */
                v5_dc_diff <= v4_scaled;
                if (v4_scaled) begin
                    s5_scaled <= s4_scaled;
                    if (ENABLE_DC_BLOCK != 0)
                        s5_dc_diff <= s4_scaled - dc_est;
                    else
                        s5_dc_diff <= {ACC_WIDTH{1'b0}};
                end

                /* 第6级：直流阻断输出。 */
                v6_dc_out <= v5_dc_diff;
                if (v5_dc_diff) begin
                    if (ENABLE_DC_BLOCK != 0) begin
                        dc_update   = s5_dc_diff >>> DC_BLOCK_SHIFT;
                        dc_est_next = dc_est + dc_update;
                        dc_est      <= dc_est_next;
                        s6_dc_removed <= s5_scaled - dc_est_next;
                    end else begin
                        dc_est        <= {ACC_WIDTH{1'b0}};
                        s6_dc_removed <= s5_scaled;
                    end
                end

                /* 第7级：FIR部分和及抽头更新。 */
                v7_fir_part <= comp_valid;
                if (comp_valid) begin
                    if (ENABLE_POST_FIR != 0) begin
                        s7_p0 <= {{4{comp_sample[ACC_WIDTH-1]}}, comp_sample} +
                                 ({{4{fir_z1[ACC_WIDTH-1]}}, fir_z1} <<< 2);

                        s7_p1 <= (({{4{fir_z2[ACC_WIDTH-1]}}, fir_z2} <<< 2) +
                                  ({{4{fir_z2[ACC_WIDTH-1]}}, fir_z2} <<< 1)) +
                                  ({{4{fir_z3[ACC_WIDTH-1]}}, fir_z3} <<< 2);

                        s7_p2 <= {{4{fir_z4[ACC_WIDTH-1]}}, fir_z4};

                        fir_z4 <= fir_z3;
                        fir_z3 <= fir_z2;
                        fir_z2 <= fir_z1;
                        fir_z1 <= comp_sample;
                    end else begin
                        s7_p0 <= {FIR_WIDTH{1'b0}};
                        s7_p1 <= {FIR_WIDTH{1'b0}};
                        s7_p2 <= {FIR_WIDTH{1'b0}};

                        fir_z1 <= {ACC_WIDTH{1'b0}};
                        fir_z2 <= {ACC_WIDTH{1'b0}};
                        fir_z3 <= {ACC_WIDTH{1'b0}};
                        fir_z4 <= {ACC_WIDTH{1'b0}};
                    end

                    s7_passthrough <= comp_sample;
                end

                /* 第8级：FIR最终和或直通。 */
                v8_fir_out <= v7_fir_part;
                if (v7_fir_part) begin
                    if (ENABLE_POST_FIR != 0) begin
                        fir_sum_final = s7_p0 + s7_p1 + s7_p2;
                        s8_pre_sat <= fir_sum_final >>> 4;
                    end else begin
                        s8_pre_sat <= s7_passthrough;
                    end
                end

                /* 第9级：int32饱和。 */
                if (v8_fir_out) begin
                    pcm_sample <= sat32(s8_pre_sat);
                    pcm_valid  <= 1'b1;
                end
            end
        end
    end

endmodule
