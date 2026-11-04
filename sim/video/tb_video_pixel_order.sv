`timescale 1ns/1ps
// 原厂实际16→64与64→16 FIFO模型：核验DDR字内像素顺序及HDMI往返。
module tb_video_pixel_order;
 reg reset=1,cam_clk=0,axi_clk=0,pix_clk=0;
 always #7 cam_clk=~cam_clk;
 always #5 axi_clk=~axi_clk;
 always #11 pix_clk=~pix_clk;
 reg [15:0] din=0;
 reg camera_write=0,wide_transfer=0,pixel_read=0;
 wire [63:0] wide;
 wire [15:0] pixel;
 wire wf_full,wf_empty,wf_wbusy,wf_rbusy,rf_full,rf_empty,rf_wbusy,rf_rbusy;
 wire [5:0] wf_reads,rf_writes;
 wire [7:0] wf_writes,rf_reads;
 integer i,wide_count=0,pixel_count=0,checks=0,handle;
 reg [63:0] expected;
 function automatic [15:0] pattern(input integer n);
  pattern=16'h1200+n*16'h0035;
 endfunction
 wr_ddr3_fifo input_fifo(.rst(reset),.wr_clk(cam_clk),.rd_clk(axi_clk),.din(din),
  .wr_en(camera_write),.rd_en(wide_transfer),.dout(wide),.full(wf_full),.empty(wf_empty),
  .rd_data_count(wf_reads),.wr_data_count(wf_writes),.wr_rst_busy(wf_wbusy),.rd_rst_busy(wf_rbusy));
 rd_ddr3_fifo output_fifo(.rst(reset),.wr_clk(axi_clk),.rd_clk(pix_clk),.din(wide),
  .wr_en(wide_transfer),.rd_en(pixel_read),.dout(pixel),.full(rf_full),.empty(rf_empty),
  .wr_data_count(rf_writes),.rd_data_count(rf_reads),.wr_rst_busy(rf_wbusy),.rd_rst_busy(rf_rbusy));
 initial begin
  handle=$fopen("fifo_words.hex","w");
  if(!handle)$fatal(1,"PIXEL_ORDER_FAIL output file");
  #120 reset=0;
  wait(!wf_wbusy&&!wf_rbusy&&!rf_wbusy&&!rf_rbusy);
  for(i=0;i<64;i=i+1)begin
   @(negedge cam_clk);
   if(wf_full)$fatal(1,"PIXEL_ORDER_FAIL unexpected full");
   din=pattern(i);camera_write=1;
  end
  @(negedge cam_clk);camera_write=0;
 end
 always @(negedge axi_clk)
  wide_transfer=!reset&&!wf_empty&&!wf_rbusy&&!rf_full&&!rf_wbusy;
 always @(posedge axi_clk)if(wide_transfer)begin
  expected={pattern(4*wide_count),pattern(4*wide_count+1),pattern(4*wide_count+2),pattern(4*wide_count+3)};
  checks=checks+1;
  if(wide!==expected)$fatal(1,"PIXEL_ORDER_FAIL wide word %0d got=%h expected=%h",wide_count,wide,expected);
  $fwrite(handle,"%016h\n",wide);
  wide_count=wide_count+1;
 end
 always @(negedge pix_clk)
  pixel_read=!reset&&!rf_empty&&!rf_rbusy;
 always @(posedge pix_clk)if(pixel_read)begin
  checks=checks+1;
  if(pixel!==pattern(pixel_count))$fatal(1,"PIXEL_ORDER_FAIL HDMI pixel %0d got=%h",pixel_count,pixel);
  pixel_count=pixel_count+1;
  if(pixel_count==64)begin
   if(wide_count!=16)$fatal(1,"PIXEL_ORDER_FAIL wide count");
   $fclose(handle);
   $display("PIXEL_ORDER_PASS words=%0d pixels=%0d checks=%0d FIRST_PIXEL_IN_MSB HDMI_ROUNDTRIP_IDENTITY",wide_count,pixel_count,checks);
   $finish;
  end
 end
 initial begin #200000;$fatal(1,"PIXEL_ORDER_FAIL timeout");end
endmodule
