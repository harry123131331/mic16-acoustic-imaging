`timescale 1ns/1ps
// 使用完整描述符，不使用未同步的多位状态连线。
module mic16_video_queue #(parameter integer WIDTH=32)(
 input wire reset,input wire wr_clk,input wire rd_clk,
 input wire [WIDTH-1:0] in_data,input wire in_valid,output wire in_ready,
 output wire [WIDTH-1:0] out_data,output wire out_valid,input wire out_ready
);
 wire full,empty,wb,rb,wr_reset,rd_reset;reg we,re;
 // XPM FIFO复位必须同步于写时钟释放；共同复位请求
 // 可能来自其他域，因此在这里同步释放。
 xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) reset_sync(
 .src_arst(reset),.dest_clk(wr_clk),.dest_arst(wr_reset));
 xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) read_reset_sync(
 .src_arst(reset),.dest_clk(rd_clk),.dest_arst(rd_reset));
 assign in_ready=!wr_reset&&!full&&!wb;assign out_valid=!rd_reset&&!empty&&!rb;
 always @* begin we=0;re=0;
  if(in_valid&&in_ready)we=1;
  if(out_valid&&out_ready)re=1;
 end
 xpm_fifo_async #(.FIFO_MEMORY_TYPE("distributed"),.FIFO_WRITE_DEPTH(16),
 .WRITE_DATA_WIDTH(WIDTH),.READ_DATA_WIDTH(WIDTH),.READ_MODE("fwft"),
 .FIFO_READ_LATENCY(0),.CDC_SYNC_STAGES(2),.RELATED_CLOCKS(0),.USE_ADV_FEATURES("0000"),
 .WR_DATA_COUNT_WIDTH(5),.RD_DATA_COUNT_WIDTH(5),.PROG_FULL_THRESH(12),.PROG_EMPTY_THRESH(3),
 .DOUT_RESET_VALUE("0"),.FULL_RESET_VALUE(0),.SIM_ASSERT_CHK(0)) fifo(
 .rst(wr_reset),.wr_clk(wr_clk),.rd_clk(rd_clk),.din(in_data),.wr_en(we),.rd_en(re),
 .dout(out_data),.full(full),.empty(empty),.wr_rst_busy(wb),.rd_rst_busy(rb),
 .sleep(1'b0),.injectsbiterr(1'b0),.injectdbiterr(1'b0));
endmodule
