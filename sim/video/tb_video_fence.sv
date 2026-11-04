`timescale 1ns/1ps
module tb_video_fence;
 reg clk=0,reset=1;always #5 clk=~clk;
 reg clear=0,busy=0,full=0;
 reg [5:0] words=0;reg empty=1;
 wire awvalid,wvalid,wlast,bready,arvalid,rready,wrpop,rdpush;
 wire [31:0] awaddr,araddr;wire [63:0] wdata,rddata;
 reg awready=0,wready=0,arready=0,rvalid=0,rlast=0,bvalid=0;
 reg [63:0] rdata=0;
 integer checks=0,violations=0,leaked=0,short_write=0,discarded=0,restart_errors=0;
 integer ar_count=0;reg [31:0] accepted_ar;
 integer source_words=0;
 always @(posedge clk)if(!reset&&wvalid&&wready)begin
  if(empty)short_write++;
  else begin source_words--;if(source_words==0)empty<=1;end
 end
`ifdef FIXED
 mic16_fifo2axi4
`else
 fifo2axi4
`endif
 #(.WR_AXI_BYTE_ADDR_BEGIN(32'h10000000),.WR_AXI_BYTE_ADDR_END(32'h10001000),
 .RD_AXI_BYTE_ADDR_BEGIN(32'h10000000),.RD_AXI_BYTE_ADDR_END(32'h10001000),
 .AXI_DATA_WIDTH(64),.AXI_ADDR_WIDTH(32),.AXI_BURST_LEN(15),.AXI_ID(0)) dut(
 .clk(clk),.reset(reset),.wr_addr_clr(1'b0),.wr_fifo_rdreq(wrpop),.wr_fifo_rddata(64'h12345678),
 .wr_fifo_empty(empty),.wr_fifo_rd_cnt(words),.wr_fifo_rst_busy(1'b0),
 .rd_addr_clr(clear),.rd_fifo_wrreq(rdpush),.rd_fifo_wrdata(rddata),
 .rd_fifo_alfull(full),.rd_fifo_wr_cnt(6'd0),.rd_fifo_rst_busy(busy),
 .m_axi_awaddr(awaddr),.m_axi_awvalid(awvalid),.m_axi_awready(awready),
 .m_axi_wdata(wdata),.m_axi_wstrb(),.m_axi_wlast(wlast),.m_axi_wvalid(wvalid),.m_axi_wready(wready),
 .m_axi_bid(4'd0),.m_axi_bresp(2'd0),.m_axi_bvalid(bvalid),.m_axi_bready(bready),
 .m_axi_araddr(araddr),.m_axi_arvalid(arvalid),.m_axi_arready(arready),
 .m_axi_rid(4'd0),.m_axi_rdata(rdata),.m_axi_rresp(2'd0),.m_axi_rlast(rlast),
 .m_axi_rvalid(rvalid),.m_axi_rready(rready));
 task check(input logic ok,input string why);
  begin checks++;if(ok!==1'b1)$fatal(1,"FENCE_HARNESS %s",why);end
 endtask
 task tick(input integer n);repeat(n)@(negedge clk);endtask
 task init_case;
  begin reset=1;clear=0;busy=0;full=0;words=0;empty=1;
   awready=0;wready=0;arready=0;rvalid=0;rlast=0;bvalid=0;
   tick(8);reset=0;tick(3);
  end
 endtask
 task wait_ar;
  begin wait(arvalid);@(negedge clk);end
 endtask
 task accept_ar;
  begin arready=1;@(posedge clk);check(arvalid,"AR present before handshake");
   accepted_ar=araddr;ar_count++;@(negedge clk);arready=0;tick(2);end
 endtask
 task send_burst(input integer startbeat,input integer endbeat,input bit expect_discard);
  begin
   for(integer i=startbeat;i<=endbeat;i++)begin
    rvalid=1;rlast=(i==15);rdata=64'hf000+i;
    @(posedge clk);
    check(rready,"receiver drains/read FIFO has space");
    check(rddata===rdata,"read bus data unmodified");
    if(expect_discard)begin if(rdpush)leaked++;else discarded++;end
    else check(rdpush,"normal current-frame word delivered");
    @(negedge clk);
   end
   rvalid=0;rlast=0;tick(3);
  end
 endtask
 initial begin
  // 取得base+128地址后阻塞ARREADY，再请求新帧。
  init_case();wait_ar();accept_ar();check(accepted_ar==32'h10000000,"first address");
  send_burst(0,15,0);wait_ar();check(araddr==32'h10000080,"second address before fence");
  clear=1;tick(3);clear=0;
  repeat(5)begin @(posedge clk);checks++;
   if(!arvalid || araddr!=32'h10000080)violations++;
   @(negedge clk);end
  accept_ar();send_burst(0,15,1);wait_ar();
  checks++;if(araddr!=32'h10000000)restart_errors++;

  // 返回数据途中清空，剩余延迟拍必须排空。
  init_case();wait_ar();accept_ar();send_burst(0,3,0);
  busy=1;clear=1;tick(2);clear=0;tick(5);
  // 复位期间保持FIFO满，证明排空模式不会因此死锁。
`ifdef FIXED
  full=1;
`endif
  send_burst(4,15,1);full=0;
  tick(5);check(!arvalid,"no new reads while FIFO reset busy");
`ifdef FIXED
  busy=1'bx;tick(3);
  check(dut.rd_flush_pending===1'b1,"unknown reset busy cannot release frame fence");
  check(!arvalid,"no AR while reset busy is unresolved");
`endif
  busy=0;tick(5);wait_ar();checks++;if(araddr!=32'h10000000)restart_errors++;

  // 近似计数可触发突发；缺数据时必须停顿W，不能从空FIFO出队。
  init_case();reset=1;full=1;busy=1;tick(8);reset=0;words=14;empty=0;
  source_words=14;awready=1;wready=1;
  repeat(100)begin @(posedge clk);checks++;@(negedge clk);end
`ifdef FIXED
  check(violations==0 && leaked==0 && short_write==0 && restart_errors==0,"all fence/threshold regressions fixed");
  $display("VIDEO_FENCE_FIXED_PASS checks=%0d ar_violations=%0d old_tail_leaks=%0d short_write=%0d restart_errors=%0d discarded=%0d",checks,violations,leaked,short_write,restart_errors,discarded);
`else
  $display("BASELINE_COUNTS ar=%0d leaked=%0d short=%0d restart=%0d",violations,leaked,short_write,restart_errors);
  check(violations>0 && leaked>0 && short_write>0,"negative baseline reproduced three defects");
  $display("VIDEO_FENCE_BASELINE_REPRODUCED checks=%0d ar_violations=%0d old_tail_leaks=%0d short_write=%0d restart_errors=%0d",checks,violations,leaked,short_write,restart_errors);
`endif
  $finish;
 end
 initial begin #100000;$fatal(1,"fence test timeout");end
endmodule
