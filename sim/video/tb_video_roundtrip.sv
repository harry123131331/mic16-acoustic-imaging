`timescale 1ns/1ps
// 真实DVP、FIFO Generator模型、控制器和显示路径，配数字AXI DDR响应器。
module tb_video_roundtrip;
 reg clk=0,pclk=0,disp_clk=0,reset=1,display_reset=1;
 always #5 clk=~clk;
 always #10 pclk=~pclk;
 always #15.277778 disp_clk=~disp_clk;
 reg vsync=1,href=0;reg [7:0] camera_byte=0;
 wire [15:0] image_data,fifo_data;wire image_valid;
 wire full,empty;wire [15:0] wr_count,rd_count;
 wire frame_begin,req,raw_hs,raw_vs,raw_de;
 wire [4:0] red,blue;wire [5:0] green;
 reg aligned_de=0;
 wire awvalid,wvalid,wlast,bready,arvalid,rready;
 wire [31:0] awaddr,araddr;wire [63:0] wdata;
 wire [7:0] awlen,arlen,wstrb;wire [2:0] awsize,arsize;
 integer cyc=0,wbeat=0,rbeat=0,wbase=0,rbase=0;
 integer write_words=0,read_words=0,checks=0,pixels=0,captured=0;
 integer write_bursts=0,read_bursts=0;
 reg write_active=0,read_active=0,bvalid=0;integer bdelay=0;
 wire writes_done=(write_words>=96000);
 wire awready=!write_active && !bvalid && !bdelay && cyc%5!=0;
 wire wready=write_active && cyc%4!=0;
 wire arready=!read_active && cyc%7!=0;
 reg [63:0] memory[0:95999];
 wire rvalid=read_active;
 wire [63:0] rdata=memory[rbase+rbeat];
 wire rlast=read_active && rbeat==15;
 wire fifo_clear=reset || !writes_done || frame_begin;
 reg [3:0] previous_fence=0;
 always @(posedge clk)if(writes_done && cyc<1574000)begin
  if({fifo_clear,dut.rd_addr_clr,dut.fifo2axi4_inst.rd_flush_pending,dut.fifo2axi4_inst.rd_fifo_rst_busy}!==previous_fence)
   $display("FENCE_DIAG cyc=%0d clr=%b addrclr=%b pending=%b busy=%b state=%h ar=%h",cyc,fifo_clear,dut.rd_addr_clr,dut.fifo2axi4_inst.rd_flush_pending,dut.fifo2axi4_inst.rd_fifo_rst_busy,dut.fifo2axi4_inst.curr_state,araddr);
  previous_fence<={fifo_clear,dut.rd_addr_clr,dut.fifo2axi4_inst.rd_flush_pending,dut.fifo2axi4_inst.rd_fifo_rst_busy};
 end
 DVP_Capture dvp(.Rst_p(reset),.PCLK(pclk),.Vsync(vsync),.Href(href),.Data(camera_byte),
  .ImageState(),.DataValid(image_valid),.DataPixel(image_data),.DataHs(),.DataVs(),.Xaddr(),.Yaddr());
 mic16_fifo_axi4_adapter #(.FIFO_DW(16),.WR_AXI_BYTE_ADDR_BEGIN(32'h10000000),
 .WR_AXI_BYTE_ADDR_END(32'h100BB800),.RD_AXI_BYTE_ADDR_BEGIN(32'h10000000),
 .RD_AXI_BYTE_ADDR_END(32'h100BB800),.AXI_DATA_WIDTH(64),.AXI_ADDR_WIDTH(32),
 .AXI_ID(0),.AXI_BURST_LEN(15)) dut(
 .clk(clk),.reset(reset),.wrfifo_clr(reset),.wrfifo_clk(pclk),.wrfifo_wren(image_valid),
 .wrfifo_din(image_data),.wrfifo_full(full),.wrfifo_wr_cnt(wr_count),
 .rdfifo_clr(fifo_clear),.rdfifo_clk(disp_clk),.rdfifo_rden(req),.rdfifo_dout(fifo_data),
 .rdfifo_empty(empty),.rdfifo_rd_cnt(rd_count),
 .m_axi_awaddr(awaddr),.m_axi_awlen(awlen),.m_axi_awsize(awsize),
 .m_axi_awvalid(awvalid),.m_axi_awready(awready),.m_axi_wdata(wdata),.m_axi_wstrb(wstrb),
 .m_axi_wlast(wlast),.m_axi_wvalid(wvalid),.m_axi_wready(wready),
 .m_axi_bid(4'd0),.m_axi_bresp(2'd0),.m_axi_bvalid(bvalid),.m_axi_bready(bready),
 .m_axi_araddr(araddr),.m_axi_arlen(arlen),.m_axi_arsize(arsize),
 .m_axi_arvalid(arvalid),.m_axi_arready(arready),.m_axi_rid(4'd0),.m_axi_rdata(rdata),
 .m_axi_rresp(2'd0),.m_axi_rlast(rlast),.m_axi_rvalid(rvalid),.m_axi_rready(rready));
 disp_driver display(.ClkDisp(disp_clk),.Rst_p(display_reset),.Data(fifo_data),.DataReq(req),
 .H_Addr(),.V_Addr(),.Disp_HS(raw_hs),.Disp_VS(raw_vs),.Disp_Red(red),.Disp_Green(green),
 .Disp_Blue(blue),.Frame_Begin(frame_begin),.Disp_DE(raw_de),.Disp_PCLK());
 always @(posedge disp_clk)if(display_reset)aligned_de<=0;else aligned_de<=raw_de;
 function automatic [15:0] pattern(input integer i);
  pattern=(i*73)^(i>>5)^16'ha55a;
 endfunction
 task check(input logic ok,input string why);
  begin checks++;if(ok!==1'b1)$fatal(1,"VIDEO_ROUNDTRIP_FAIL %s cyc=%0d pix=%0d",why,cyc,pixels);end
 endtask
 // 独立DDR模型，每方向一笔事务，128字节对齐。
 always @(posedge clk)if(!reset)begin
  cyc<=cyc+1;
  if(bdelay>0)begin bdelay<=bdelay-1;if(bdelay==1)bvalid<=1;end
  if(bvalid&&bready)bvalid<=0;
  if(awvalid&&awready)begin
   check(awlen==15&&awsize==3&&awaddr[6:0]==0,"write geometry");
   check(awaddr==32'h10000000+write_words*8,"monotonic capture address");
   wbase=(awaddr-32'h10000000)/8;wbeat=0;write_active<=1;write_bursts++;
  end
  if(wvalid&&wready)begin
   check(write_active&&wstrb==8'hff&&wlast==(wbeat==15),"write transfer geometry");
   check(wbase+wbeat<96000,"write stays in framebuffer");
   memory[wbase+wbeat]<=wdata;write_words++;
   if(wlast)begin write_active<=0;bdelay<=8;end
   wbeat++;
  end
  if(arvalid&&arready)begin
   if(read_bursts<4)$display("AR_DIAG addr=%h cyc=%0d pending=%b clr=%b busy=%b",araddr,cyc,dut.fifo2axi4_inst.rd_flush_pending,dut.rd_addr_clr,dut.fifo2axi4_inst.rd_fifo_rst_busy);
   check(writes_done,"no read of incomplete startup frame");
   check(arlen==15&&arsize==3&&araddr[6:0]==0,"read geometry");
   check(araddr>=32'h10000000&&araddr<32'h100BB800,"read stays in framebuffer");
   rbase<=(araddr-32'h10000000)/8;rbeat<=0;read_active<=1;read_bursts++;
  end
  if(rvalid&&rready)begin
   if(read_words<32 && (rbeat==0||rbeat==15))$display("R_DIAG base=%0d beat=%0d data=%h push=%b drop=%b",rbase,rbeat,rdata,dut.rdfifo_wren,dut.fifo2axi4_inst.rd_drop);
   read_words++;
   if(rlast)read_active<=0;
   rbeat<=rbeat+1;
  end
 end
 always @(posedge pclk)if(!reset&&image_valid)begin
  check(!full,"no capture FIFO overflow under declared stalls");
  check(image_data===pattern(captured),"DVP byte/pixel order");captured++;
 end
 always @(posedge disp_clk)if(!display_reset&&req)
  check(!empty,"no display FIFO underflow under declared stalls");
 always @(negedge disp_clk)if(!display_reset&&aligned_de)begin
  if({red,green,blue}!==pattern(pixels%384000))
   $display("PIXEL_DIAG actual=%h expected=%h memory0=%h fifo=%h reads=%0d",{red,green,blue},pattern(pixels%384000),memory[0],fifo_data,read_words);
  check({red,green,blue}===pattern(pixels%384000),"full RGB565 roundtrip order");
  pixels++;
  if(pixels==768000)begin
   check(captured==384000&&write_words==96000&&write_bursts==6000,"exact one captured frame");
   $display("VIDEO_ROUNDTRIP_PASS pixels=%0d captured=%0d checks=%0d write_bursts=%0d read_bursts=%0d",pixels,captured,checks,write_bursts,read_bursts);$finish;
  end
 end
 initial begin
  repeat(20)@(negedge pclk);reset=0;
  // 传感器启动先给十个无负载VSYNC边界，遵循原厂要求。
  repeat(12)begin vsync=1;repeat(8)@(negedge pclk);vsync=0;repeat(8)@(negedge pclk);end
  repeat(20)@(negedge pclk);
  for(integer y=0;y<480;y++)begin
   href=1;
   for(integer x=0;x<800;x++)begin
    camera_byte=pattern(y*800+x)>>8;@(negedge pclk);
    camera_byte=pattern(y*800+x);@(negedge pclk);
   end
   href=0;camera_byte=0;repeat(32)@(negedge pclk);
  end
  vsync=1;wait(writes_done);repeat(100)@(negedge clk);
  @(negedge disp_clk);display_reset=0;
 end
 initial begin #70000000;$fatal(1,"roundtrip timeout words=%0d pix=%0d cap=%0d",write_words,pixels,captured);end
endmodule
