`timescale 1ns/1ps
module vendor_axi_case #(parameter integer SHIFT=0)(output reg done,output integer violations,output integer checks);
 reg clk=0,reset=1;always #5 clk=~clk;
 integer cycle=0,writes=0,reads=0,wbeat=0,rbeat=0;
 reg write_active=0,read_active=0,bvalid=0;
 wire [31:0] awaddr,araddr;wire [7:0] awlen,arlen,wstrb;
 wire [2:0] awsize,arsize;wire [1:0] awburst,arburst;
 wire awvalid,wvalid,wlast,bready,arvalid,rready;
 wire awready=!write_active && !bvalid && cycle%3!=0;
 wire wready=write_active && cycle%4!=0;
 wire arready=!read_active && cycle%5!=0;
 wire rvalid=read_active;wire rlast=(rbeat==15);
 wire [63:0] wdata;
 wire [63:0] read_data=64'h100+rbeat;
 wire wr_pop,rd_push;wire [63:0] rd_data;
 reg [63:0] source_data=0;
 reg aw_stalled=0,w_stalled=0,ar_stalled=0;
 reg [31:0] old_aw,old_ar;reg [72:0] old_w;
 fifo2axi4 #(.WR_AXI_BYTE_ADDR_BEGIN(32'h10000000+SHIFT),.WR_AXI_BYTE_ADDR_END(32'h10000100),
  .RD_AXI_BYTE_ADDR_BEGIN(32'h10000000+SHIFT),.RD_AXI_BYTE_ADDR_END(32'h10000100),
  .AXI_DATA_WIDTH(64),.AXI_ADDR_WIDTH(32),.AXI_BURST_LEN(15),.AXI_ID(0)) dut(
  .clk(clk),.reset(reset),.wr_addr_clr(1'b0),.wr_fifo_rdreq(wr_pop),.wr_fifo_rddata(source_data),
  .wr_fifo_empty(1'b0),.wr_fifo_rd_cnt(6'd32),.wr_fifo_rst_busy(1'b0),
  .rd_addr_clr(1'b0),.rd_fifo_wrreq(rd_push),.rd_fifo_wrdata(rd_data),
  .rd_fifo_alfull(cycle%7==0),.rd_fifo_wr_cnt(6'd0),.rd_fifo_rst_busy(1'b0),
  .m_axi_awaddr(awaddr),.m_axi_awlen(awlen),.m_axi_awsize(awsize),.m_axi_awburst(awburst),
  .m_axi_awvalid(awvalid),.m_axi_awready(awready),.m_axi_wdata(wdata),.m_axi_wstrb(wstrb),
  .m_axi_wlast(wlast),.m_axi_wvalid(wvalid),.m_axi_wready(wready),
  .m_axi_bid(4'd0),.m_axi_bresp(2'd0),.m_axi_bvalid(bvalid),.m_axi_bready(bready),
  .m_axi_araddr(araddr),.m_axi_arlen(arlen),.m_axi_arsize(arsize),.m_axi_arburst(arburst),
  .m_axi_arvalid(arvalid),.m_axi_arready(arready),.m_axi_rid(4'd0),.m_axi_rdata(read_data),
  .m_axi_rresp(2'd0),.m_axi_rlast(rlast),.m_axi_rvalid(rvalid),.m_axi_rready(rready));
 task check(input logic test,input string why);
  begin checks=checks+1;if(test!==1'b1)$fatal(1,"VENDOR_AXI_FAIL shift=%0d %s",SHIFT,why);end
 endtask
 initial begin done=0;violations=0;checks=0;repeat(8)@(negedge clk);reset=0;end
 always @(posedge clk)if(!reset && !done)begin
  cycle<=cycle+1;
  if(aw_stalled)check(awvalid && awaddr===old_aw,"AW stable under backpressure");
  if(w_stalled)check(wvalid && {wlast,wstrb,wdata}===old_w,"W stable under backpressure");
  if(ar_stalled)check(arvalid && araddr===old_ar,"AR stable under backpressure");
  aw_stalled=awvalid&&!awready;old_aw=awaddr;
  w_stalled=wvalid&&!wready;old_w={wlast,wstrb,wdata};
  ar_stalled=arvalid&&!arready;old_ar=araddr;
  if(bvalid&&bready)bvalid<=0;
  if(awvalid&&awready)begin
   check(awlen==15 && awsize==3 && awburst==1,"write burst geometry");
   check(awaddr==32'h10000000+SHIFT+(writes%2)*128,"write frame wrap");
   if(awaddr[2:0]!=0)violations=violations+1;
   write_active<=1;wbeat=0;
  end
  if(wvalid&&wready)begin
   check(write_active && wlast==(wbeat==15) && wstrb==8'hff,"exact WLAST and full byte lanes");
   check(wdata===source_data,"FIFO word ordering");
   source_data<=source_data+1;
   if(wlast)begin writes=writes+1;write_active<=0;bvalid<=1;end
   wbeat=wbeat+1;
  end
  if(arvalid&&arready)begin
   check(arlen==15 && arsize==3 && arburst==1,"read burst geometry");
   check(araddr==32'h10000000+SHIFT+(reads%2)*128,"read frame wrap");
   if(araddr[2:0]!=0)violations=violations+1;
   read_active<=1;rbeat=0;
  end
  if(rvalid&&rready)begin
   check(rd_push && rd_data===read_data,"read FIFO word ordering");
   if(rlast)begin reads=reads+1;read_active<=0;end
   rbeat=rbeat+1;
  end
  if(writes>=8 && reads>=8)begin
   if(SHIFT==0)check(violations==0,"aligned candidate");
   else check(violations>=16,"original plus-one baseline exposes alignment mismatch");
   done<=1;
  end
 end
endmodule
module tb_vendor_axi;
 wire good_done,bad_done;wire [31:0] good_violations,bad_violations,good_checks,bad_checks;
 vendor_axi_case #(.SHIFT(0)) candidate(good_done,good_violations,good_checks);
 vendor_axi_case #(.SHIFT(1)) original(bad_done,bad_violations,bad_checks);
 initial begin
  wait(good_done && bad_done);
  $display("AV_VENDOR_AXI_PASS checks=%0d candidate_alignment=%0d original_alignment=%0d",good_checks+bad_checks,good_violations,bad_violations);$finish;
 end
 initial begin #100000;$fatal(1,"vendor AXI timeout");end
endmodule
