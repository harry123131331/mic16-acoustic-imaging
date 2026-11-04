/* 生成共享PDM时钟及延迟采样脉冲。clk_ref=100MHz、分频参数20时，PDM_CLK=100MHz/(2×20)=2.5MHz。 */
module pdm_clk_gen #(
    parameter integer SAMPLE_EDGE  = 0,   // 0表示下降沿后采样，1表示上升沿后采样。
    parameter integer SAMPLE_DELAY = 3,   // 延迟计数器；实际采样脉冲约延后SAMPLE_DELAY+1个参考时钟。
    parameter integer MIN_DIV      = 16   // 防止PDM时钟过高。
)(
    input  wire        clk_ref,
    input  wire        rst_n,
    input  wire        enable,
    input  wire [15:0] pdm_clk_div,

    output reg         pdm_clk,
    output reg         pdm_sample_tick,
    output reg         pdm_clk_running
);

    reg [15:0] div_cnt;
    reg [7:0]  delay_cnt;
    reg        arm_sample;

    localparam [15:0] MIN_DIV_U16 = MIN_DIV;

    wire [15:0] div_safe;
    wire        target_edge_before_toggle;

    assign div_safe = (pdm_clk_div < MIN_DIV_U16) ? MIN_DIV_U16 : pdm_clk_div;

    /* 翻转前pdm_clk=1则即将出现下降沿；原值为0则即将出现上升沿。 */
    assign target_edge_before_toggle =
        (SAMPLE_EDGE == 0) ? (pdm_clk == 1'b1) : (pdm_clk == 1'b0);

    // proc_sys_reset保持复位多个参考时钟周期，因此使用
    // 同步复位可保持行为并改善逻辑单元打包。
    always @(posedge clk_ref) begin
        if (!rst_n) begin
            div_cnt         <= 16'd0;
            delay_cnt       <= 8'd0;
            arm_sample      <= 1'b0;
            pdm_clk         <= 1'b0;
            pdm_sample_tick <= 1'b0;
            pdm_clk_running <= 1'b0;
        end else begin
            pdm_sample_tick <= 1'b0;

            if (!enable) begin
                div_cnt         <= 16'd0;
                delay_cnt       <= 8'd0;
                arm_sample      <= 1'b0;
                pdm_clk         <= 1'b0;
                pdm_sample_tick <= 1'b0;
                pdm_clk_running <= 1'b0;
            end else begin
                pdm_clk_running <= 1'b1;

                if (div_cnt >= div_safe - 16'd1) begin
                    div_cnt <= 16'd0;

                    if (target_edge_before_toggle) begin
                        delay_cnt  <= SAMPLE_DELAY[7:0];
                        arm_sample <= 1'b1;
                    end

                    pdm_clk <= ~pdm_clk;
                end else begin
                    div_cnt <= div_cnt + 16'd1;
                end

                if (arm_sample) begin
                    if (delay_cnt == 8'd0) begin
                        pdm_sample_tick <= 1'b1;
                        arm_sample      <= 1'b0;
                    end else begin
                        delay_cnt <= delay_cnt - 8'd1;
                    end
                end
            end
        end
    end

endmodule
