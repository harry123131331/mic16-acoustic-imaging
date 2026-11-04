`timescale 1ns/1ps
module tb_video_registers;
 reg clk=0,video_clk=0,pix_clk=0,resetn=0;
 always #5 clk=~clk;always #7 video_clk=~video_clk;always #11 pix_clk=~pix_clk;
 reg [7:0] araddr=0;reg arvalid=0,rready=0;wire arready,rvalid;wire [31:0] rdata;wire [1:0] rresp;
 reg [31:0] status=32'h12345678;
 integer checks=0;
 mic16_av_overlay_video dut(.s_axi_aclk(clk),.pix_clk(pix_clk),.resetn(resetn),.video_clk(video_clk),
 .s_axi_awaddr(8'd0),.s_axi_awprot(3'd0),.s_axi_awvalid(1'b0),.s_axi_awready(),
 .s_axi_wdata(32'd0),.s_axi_wstrb(4'd0),.s_axi_wvalid(1'b0),.s_axi_wready(),
 .s_axi_bresp(),.s_axi_bvalid(),.s_axi_bready(1'b1),
 .s_axi_araddr(araddr),.s_axi_arprot(3'd0),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
 .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
 .frame_start(1'b0),.pixel_x(12'd0),.pixel_y(12'd0),.in_de(1'b0),.in_hs(1'b0),.in_vs(1'b0),.in_rgb(24'd0),
 .out_de(),.out_hs(),.out_vs(),.out_rgb(),.active_cycle_id(),.marker_visible(),
 .video_status(status),.video_complete(32'd11),.video_reject(32'd12),.video_repeat(32'd13),
 .video_supersede(32'd14),.video_underflow(32'd15),.video_errors(32'd16),.video_seq(32'd17));
 task check(input logic ok,input string why);begin checks++;if(ok!==1'b1)$fatal(1,"VIDEO_REG_FAIL %s",why);end endtask
 task query(input [7:0] addr,input [31:0] expected,input [1:0] response);
  begin
   @(negedge clk);araddr=addr;arvalid=1;
   do @(posedge clk);while(!arready);
   @(negedge clk);arvalid=0;
   wait(rvalid);@(negedge clk);
   check(rdata===expected&&rresp===response,"muxed register value/response");
   repeat(7)begin @(posedge clk);check(rvalid&&rdata===expected&&rresp===response,"held R response");end
   @(negedge clk);rready=1;@(negedge clk);rready=0;
  end
 endtask
 initial begin
  #300;resetn=1;#1000;
  query(0,32'h4d415631,0);query(8'h80,status,0);
  for(integer i=0;i<7;i++)begin query(8'h84+i*4,11+i,0);query(4,32'h10000,0);end
  query(8'h81,0,2);query(8'ha0,0,2);query(6,0,2);
  // 源计数器变化且RREADY拉低时，快照必须保持稳定。
  @(negedge clk);araddr=8'h80;arvalid=1;do @(posedge clk);while(!arready);
  @(negedge clk);arvalid=0;wait(rvalid);status=32'hfedcba98;
  repeat(10)begin @(posedge clk);check(rvalid&&rdata==32'h12345678,"snapshot remains stable during source change");end
  @(negedge clk);rready=1;@(negedge clk);rready=0;query(8'h80,status,0);
  // 描述符在途时共同复位必须丢弃旧响应。
  @(negedge clk);araddr=8'h84;arvalid=1;do @(posedge clk);while(!arready);
  @(negedge clk);arvalid=0;resetn=0;#300;resetn=1;#1000;
  check(!rvalid,"reset flushes pending diagnostic transaction");query(0,32'h4d415631,0);
  $display("VIDEO_REGISTERS_PASS checks=%0d",checks);$finish;
 end
 initial begin #100000;$fatal(1,"register timeout");end
endmodule
