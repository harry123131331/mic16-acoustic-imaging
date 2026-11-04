/* 16路PDM顶层。当前参数：参考100MHz，分频20，PDM为2.5MHz，抽取64，PCM采样率39062.5Hz；16通道、每包16样本/通道，AXIS负载1024字节。 */
module pl_pdm_top_16 #(
    parameter integer DECIMATION            = 64,
    parameter integer SCALE_SHIFT           = 12,
    parameter integer SAMPLES_PER_PACKET    = 16,
    parameter integer AXIS_GROUP_FIFO_DEPTH = 8,
    /* DECIMATION=64时，有符号1位输入的CIC3增长约2+3log2(64)=20位。保留12位余量，避免每通道48位实现的资源开销。 */
    parameter integer CIC_ACC_WIDTH         = 32,
    parameter integer ENABLE_DC_BLOCK       = 1,
    parameter integer DC_BLOCK_SHIFT        = 10,
    parameter integer ENABLE_CIC_COMP       = 1,
    parameter integer ENABLE_POST_FIR       = 1,
    /* 仅新数据流BD启用。原BD默认0，保留原16路滤波实现作为基线。 */
    parameter integer SHARED_DECIMATOR      = 0
)(
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk_ref CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF m_axis, ASSOCIATED_RESET rst_n, FREQ_HZ 100000000" *)
    input  wire        clk_ref,

    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rst_n RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        rst_n,

    input  wire        enable,
    input  wire [15:0] pdm_clk_div,

    input  wire        pdm_data0,
    input  wire        pdm_data1,
    input  wire        pdm_data2,
    input  wire        pdm_data3,
    input  wire        pdm_data4,
    input  wire        pdm_data5,
    input  wire        pdm_data6,
    input  wire        pdm_data7,
    input  wire        pdm_data8,
    input  wire        pdm_data9,
    input  wire        pdm_data10,
    input  wire        pdm_data11,
    input  wire        pdm_data12,
    input  wire        pdm_data13,
    input  wire        pdm_data14,
    input  wire        pdm_data15,

    output wire        pdm_clk_out,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TDATA" *)
    output wire [31:0] m_axis_tdata,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TKEEP" *)
    output wire [3:0]  m_axis_tkeep,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TLAST" *)
    output wire        m_axis_tlast,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TVALID" *)
    output wire        m_axis_tvalid,

    (* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TREADY" *)
    input  wire        m_axis_tready,

    output wire [3:0]  dbg_channel,
    output wire [15:0] dbg_sample_index,
    output wire [15:0] dbg_fifo_level,
    output wire        pdm_clk_running,
    output wire        overflow_error,
    output wire [31:0] overflow_count
);

    localparam integer CHANNEL_COUNT = 16;

    wire        pdm_sample_tick;
    wire [15:0] pdm_clk_div_eff;

    wire [CHANNEL_COUNT-1:0] pdm_data_bus;
    wire [CHANNEL_COUNT-1:0] pcm_valids;
    wire [CHANNEL_COUNT*32-1:0] pcm_samples_flat;
    wire shared_schedule_error,pack_overflow_error;
    wire [31:0] shared_schedule_error_count,pack_overflow_count;
    wire [32:0] total_overflows={1'b0,shared_schedule_error_count}+{1'b0,pack_overflow_count};
    assign overflow_error=shared_schedule_error|pack_overflow_error;
    assign overflow_count=total_overflows[32]?32'hffffffff:total_overflows[31:0];

    genvar gi;

    /* 软件未写pdm_clk_div时使用默认20。 */
    assign pdm_clk_div_eff = (pdm_clk_div == 16'd0) ? 16'd20 : pdm_clk_div;

    assign pdm_data_bus[0]  = pdm_data0;
    assign pdm_data_bus[1]  = pdm_data1;
    assign pdm_data_bus[2]  = pdm_data2;
    assign pdm_data_bus[3]  = pdm_data3;
    assign pdm_data_bus[4]  = pdm_data4;
    assign pdm_data_bus[5]  = pdm_data5;
    assign pdm_data_bus[6]  = pdm_data6;
    assign pdm_data_bus[7]  = pdm_data7;
    assign pdm_data_bus[8]  = pdm_data8;
    assign pdm_data_bus[9]  = pdm_data9;
    assign pdm_data_bus[10] = pdm_data10;
    assign pdm_data_bus[11] = pdm_data11;
    assign pdm_data_bus[12] = pdm_data12;
    assign pdm_data_bus[13] = pdm_data13;
    assign pdm_data_bus[14] = pdm_data14;
    assign pdm_data_bus[15] = pdm_data15;

    pdm_clk_gen #(
        .SAMPLE_EDGE  (0),
        .SAMPLE_DELAY (3),
        .MIN_DIV      (16)
    ) u_pdm_clk_gen (
        .clk_ref         (clk_ref),
        .rst_n           (rst_n),
        .enable          (enable),
        .pdm_clk_div     (pdm_clk_div_eff),
        .pdm_clk         (pdm_clk_out),
        .pdm_sample_tick (pdm_sample_tick),
        .pdm_clk_running (pdm_clk_running)
    );

    generate
        if (SHARED_DECIMATOR != 0) begin : GEN_SHARED
            wire group_valid;
            pdm_decimator_shared #(
                .DECIMATION(DECIMATION),.SCALE_SHIFT(SCALE_SHIFT),.ACC_WIDTH(CIC_ACC_WIDTH),
                .ENABLE_DC_BLOCK(ENABLE_DC_BLOCK),.DC_BLOCK_SHIFT(DC_BLOCK_SHIFT),
                .ENABLE_CIC_COMP(ENABLE_CIC_COMP),.ENABLE_POST_FIR(ENABLE_POST_FIR)
            ) u_shared_decimator (
                .clk_ref(clk_ref),.rst_n(rst_n),.enable(enable),.pdm_sample_tick(pdm_sample_tick),
                .pdm_data(pdm_data_bus),.pcm_samples_flat(pcm_samples_flat),.pcm_valid(group_valid),
                .schedule_error(shared_schedule_error),.schedule_error_count(shared_schedule_error_count)
            );
            assign pcm_valids={CHANNEL_COUNT{group_valid}};
        end else begin : GEN_PARALLEL
        assign shared_schedule_error=1'b0;
        assign shared_schedule_error_count=32'd0;
        for (gi = 0; gi < CHANNEL_COUNT; gi = gi + 1) begin : GEN_DECIMATOR
            wire signed [31:0] pcm_sample_i;

            pdm_decimator #(
                .DECIMATION      (DECIMATION),
                .SCALE_SHIFT     (SCALE_SHIFT),
                .ACC_WIDTH       (CIC_ACC_WIDTH),
                .ENABLE_DC_BLOCK (ENABLE_DC_BLOCK),
                .DC_BLOCK_SHIFT  (DC_BLOCK_SHIFT),
                .ENABLE_CIC_COMP (ENABLE_CIC_COMP),
                .ENABLE_POST_FIR (ENABLE_POST_FIR)
            ) u_decim (
                .clk_ref         (clk_ref),
                .rst_n           (rst_n),
                .enable          (enable),
                .pdm_sample_tick (pdm_sample_tick),
                .pdm_data        (pdm_data_bus[gi]),
                .pcm_sample      (pcm_sample_i),
                .pcm_valid       (pcm_valids[gi])
            );

            assign pcm_samples_flat[gi*32 +: 32] = pcm_sample_i;
        end
        end
    endgenerate

    pdm_axis_pack_nch #(
        .CHANNEL_COUNT      (CHANNEL_COUNT),
        .SAMPLES_PER_PACKET (SAMPLES_PER_PACKET),
        .FIFO_DEPTH         (AXIS_GROUP_FIFO_DEPTH)
    ) u_axis_pack (
        .clk              (clk_ref),
        .rst_n            (rst_n),
        .enable           (enable),

        .pcm_samples_flat (pcm_samples_flat),
        .pcm_valids       (pcm_valids),

        .m_axis_tdata     (m_axis_tdata),
        .m_axis_tkeep     (m_axis_tkeep),
        .m_axis_tlast     (m_axis_tlast),
        .m_axis_tvalid    (m_axis_tvalid),
        .m_axis_tready    (m_axis_tready),

        .dbg_channel      (dbg_channel),
        .dbg_sample_index (dbg_sample_index),
        .overflow_error   (pack_overflow_error),
        .dbg_fifo_level   (dbg_fifo_level),
        .overflow_count   (pack_overflow_count)
    );

endmodule
