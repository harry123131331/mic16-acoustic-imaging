/* 将N通道PCM封装为32位AXI4-Stream。16通道、每包每通道16样本时，总长16×16×4=1024字节。按样本优先排列：样本0通道0～15，样本1通道0～15，直到样本15通道0～15。 */
module pdm_axis_pack_nch #(
    parameter integer CHANNEL_COUNT      = 16,
    parameter integer SAMPLES_PER_PACKET = 16,
    parameter integer FIFO_DEPTH         = 8
)(
    input  wire                              clk,
    input  wire                              rst_n,
    input  wire                              enable,

    input  wire [CHANNEL_COUNT*32-1:0]       pcm_samples_flat,
    input  wire [CHANNEL_COUNT-1:0]          pcm_valids,

    output reg  [31:0]                       m_axis_tdata,
    output wire [3:0]                        m_axis_tkeep,
    output reg                               m_axis_tlast,
    output reg                               m_axis_tvalid,
    input  wire                              m_axis_tready,

    output reg  [3:0]                        dbg_channel,
    output reg  [15:0]                       dbg_sample_index,
    output reg                               overflow_error,
    output wire [15:0]                       dbg_fifo_level,
    output reg  [31:0]                       overflow_count
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

    localparam integer CH_WIDTH    = clog2(CHANNEL_COUNT);
    localparam integer PTR_WIDTH   = clog2(FIFO_DEPTH);
    localparam integer COUNT_WIDTH = clog2(FIFO_DEPTH + 1);

    localparam [CH_WIDTH-1:0] CH_LAST = CHANNEL_COUNT - 1;
    localparam [COUNT_WIDTH-1:0] FIFO_DEPTH_U = FIFO_DEPTH;

    reg [CH_WIDTH-1:0]            send_ch;
    reg [15:0]                    sample_index;
    reg                           sending;
    reg [CHANNEL_COUNT*32-1:0]    samples_latched;

    reg [CHANNEL_COUNT*32-1:0]    group_fifo [0:FIFO_DEPTH-1];
    reg [PTR_WIDTH-1:0]           wr_ptr;
    reg [PTR_WIDTH-1:0]           rd_ptr;
    reg [COUNT_WIDTH-1:0]         fifo_count;

    wire all_pcm_valid;
    wire fifo_full;
    wire fifo_empty;
    wire start_send;
    wire write_fire;
    wire overflow_fire;

    assign m_axis_tkeep = 4'hF;

    assign all_pcm_valid = &pcm_valids;
    assign fifo_full     = (fifo_count == FIFO_DEPTH_U);
    assign fifo_empty    = (fifo_count == {COUNT_WIDTH{1'b0}});
    assign start_send    = enable && !sending && !m_axis_tvalid && !fifo_empty;
    assign dbg_fifo_level = {{(16-COUNT_WIDTH){1'b0}}, fifo_count};

    /* FIFO满时禁止写入，避免满状态同时读写的含糊行为。 */
    assign write_fire    = enable && all_pcm_valid && !fifo_full;
    assign overflow_fire = enable && all_pcm_valid && fifo_full;

    function [PTR_WIDTH-1:0] inc_ptr;
        input [PTR_WIDTH-1:0] ptr;
        begin
            if (ptr == FIFO_DEPTH - 1)
                inc_ptr = {PTR_WIDTH{1'b0}};
            else
                inc_ptr = ptr + {{(PTR_WIDTH-1){1'b0}}, 1'b1};
        end
    endfunction

    function [31:0] select_sample;
        input [CHANNEL_COUNT*32-1:0] flat;
        input [CH_WIDTH-1:0] ch;
        begin
            select_sample = flat[ch*32 +: 32];
        end
    endfunction

    // BD中的复位源与clk同步；采用同步写法
    // 还可避免异步BRAM控制引起的DRC警告。
    always @(posedge clk) begin
        if (!rst_n) begin
            send_ch          <= {CH_WIDTH{1'b0}};
            sample_index     <= 16'd0;
            sending          <= 1'b0;
            samples_latched  <= {(CHANNEL_COUNT*32){1'b0}};

            wr_ptr           <= {PTR_WIDTH{1'b0}};
            rd_ptr           <= {PTR_WIDTH{1'b0}};
            fifo_count       <= {COUNT_WIDTH{1'b0}};

            m_axis_tdata     <= 32'd0;
            m_axis_tlast     <= 1'b0;
            m_axis_tvalid    <= 1'b0;

            dbg_channel      <= 4'd0;
            dbg_sample_index <= 16'd0;
            overflow_error   <= 1'b0;
            overflow_count   <= 32'd0;
        end else begin
            if (!enable) begin
                send_ch          <= {CH_WIDTH{1'b0}};
                sample_index     <= 16'd0;
                sending          <= 1'b0;
                samples_latched  <= {(CHANNEL_COUNT*32){1'b0}};

                wr_ptr           <= {PTR_WIDTH{1'b0}};
                rd_ptr           <= {PTR_WIDTH{1'b0}};
                fifo_count       <= {COUNT_WIDTH{1'b0}};

                m_axis_tdata     <= 32'd0;
                m_axis_tlast     <= 1'b0;
                m_axis_tvalid    <= 1'b0;

                dbg_channel      <= 4'd0;
                dbg_sample_index <= 16'd0;
                overflow_error   <= 1'b0;
                overflow_count   <= 32'd0;
            end else begin
                if (overflow_fire) begin
                    overflow_error <= 1'b1;
                    overflow_count <= overflow_count + 32'd1;
                end

                if (write_fire) begin
                    group_fifo[wr_ptr] <= pcm_samples_flat;
                    wr_ptr <= inc_ptr(wr_ptr);
                end

                if (start_send) begin
                    samples_latched <= group_fifo[rd_ptr];
                    rd_ptr          <= inc_ptr(rd_ptr);

                    send_ch       <= {CH_WIDTH{1'b0}};
                    sending       <= 1'b1;
                    m_axis_tvalid <= 1'b1;
                    m_axis_tdata  <= select_sample(group_fifo[rd_ptr], {CH_WIDTH{1'b0}});
                    m_axis_tlast  <= (CHANNEL_COUNT == 1) && (sample_index == SAMPLES_PER_PACKET - 1);

                    dbg_channel      <= 4'd0;
                    dbg_sample_index <= sample_index;
                end

                case ({write_fire, start_send})
                    2'b10: fifo_count <= fifo_count + {{(COUNT_WIDTH-1){1'b0}}, 1'b1};
                    2'b01: fifo_count <= fifo_count - {{(COUNT_WIDTH-1){1'b0}}, 1'b1};
                    default: fifo_count <= fifo_count;
                endcase

                if (m_axis_tvalid && m_axis_tready) begin
                    if (send_ch == CH_LAST) begin
                        m_axis_tvalid <= 1'b0;
                        m_axis_tlast  <= 1'b0;
                        sending       <= 1'b0;
                        send_ch       <= {CH_WIDTH{1'b0}};

                        if (sample_index == SAMPLES_PER_PACKET - 1)
                            sample_index <= 16'd0;
                        else
                            sample_index <= sample_index + 16'd1;
                    end else begin
                        send_ch      <= send_ch + {{(CH_WIDTH-1){1'b0}}, 1'b1};
                        m_axis_tdata <= select_sample(
                            samples_latched,
                            send_ch + {{(CH_WIDTH-1){1'b0}}, 1'b1}
                        );

                        if ((sample_index == SAMPLES_PER_PACKET - 1) &&
                            ((send_ch + {{(CH_WIDTH-1){1'b0}}, 1'b1}) == CH_LAST))
                            m_axis_tlast <= 1'b1;
                        else
                            m_axis_tlast <= 1'b0;

                        dbg_channel      <= send_ch + {{(CH_WIDTH-1){1'b0}}, 1'b1};
                        dbg_sample_index <= sample_index;
                    end
                end
            end
        end
    end

endmodule
