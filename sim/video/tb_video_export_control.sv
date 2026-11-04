`timescale 1ns/1ps
// 真实旧叠加寄存器、XPM跨域队列和新所有权核心联合检查。
module tb_video_export_control;
 reg c=0,v=0,p=0,reset=1;always #5 c=~c;always #7 v=~v;always #11 p=~p;
 reg [7:0] aw=0,ar=0;reg av=0,wv=0,arv=0,br=0,rr=0;reg [31:0] wd=0;reg [3:0] st=15;
 wire ard,wrd,bv,arr,rv;wire [1:0] bp,rp;wire [31:0] rd;
 wire [7:0] law,lar;wire lav,lawr,lwv,lwr,lbv,lbr,larv,larr,lrv,lrr;
 wire [31:0] lwd,lrd;wire [3:0] lst;wire [1:0] lbp,lrp;
 wire acq,can_acq,held,rel,accepted,exhausted;wire [1:0] bank;wire [31:0] seq,token,reltoken;
 reg allocate=0,finish=0,sw=0;reg [1:0] fb=0;reg [31:0] fs=0;
 wire available;wire [1:0] allocation;wire [2:0] free_mask;
 reg [1:0] saved_bank;reg [31:0] value,saved_token;integer checks=0,identity;
 mic16_video_export_regs dut(.reset(reset),.ctrl_clk(c),.video_clk(v),
 .awaddr(aw),.awvalid(av),.awready(ard),.wdata(wd),.wstrb(st),.wvalid(wv),.wready(wrd),.bresp(bp),.bvalid(bv),.bready(br),
 .araddr(ar),.arvalid(arv),.arready(arr),.rdata(rd),.rresp(rp),.rvalid(rv),.rready(rr),
 .legacy_awaddr(law),.legacy_awvalid(lav),.legacy_awready(lawr),.legacy_wdata(lwd),.legacy_wstrb(lst),.legacy_wvalid(lwv),.legacy_wready(lwr),
 .legacy_bresp(lbp),.legacy_bvalid(lbv),.legacy_bready(lbr),.legacy_araddr(lar),.legacy_arvalid(larv),.legacy_arready(larr),
 .legacy_rdata(lrd),.legacy_rresp(lrp),.legacy_rvalid(lrv),.legacy_rready(lrr),
 .acquire_valid(acq),.acquire_ready(can_acq),.export_valid(held),.export_bank(bank),.export_seq(seq),.export_token(token),
 .release_valid(rel),.release_token(reltoken),.release_accepted(accepted),.token_exhausted(exhausted));
 mic16_frame_banks_export banks(.clk(v),.reset(reset),.allocate(allocate),.available(available),.allocation(allocation),
 .finish(finish),.finish_good(1'b1),.finish_bank(fb),.finish_seq(fs),.switch_display(sw),
 .next_display_valid(),.next_display_bank(),.next_display_seq(),.display_valid(),.display_bank(),.display_seq(),
 .ready_valid(),.ready_bank(),.ready_seq(),.free_mask(free_mask),.superseded(),
 .acquire_valid(acq),.acquire_ready(can_acq),.export_valid(held),.export_bank(bank),.export_seq(seq),.export_token(token),
 .release_valid(rel),.release_token(reltoken),.release_accepted(accepted),.token_exhausted(exhausted));
 mic16_av_overlay_video legacy(.s_axi_aclk(c),.pix_clk(p),.resetn(!reset),.video_clk(v),
 .s_axi_awaddr(law),.s_axi_awprot(3'd0),.s_axi_awvalid(lav),.s_axi_awready(lawr),
 .s_axi_wdata(lwd),.s_axi_wstrb(lst),.s_axi_wvalid(lwv),.s_axi_wready(lwr),.s_axi_bresp(lbp),.s_axi_bvalid(lbv),.s_axi_bready(lbr),
 .s_axi_araddr(lar),.s_axi_arprot(3'd0),.s_axi_arvalid(larv),.s_axi_arready(larr),.s_axi_rdata(lrd),.s_axi_rresp(lrp),.s_axi_rvalid(lrv),.s_axi_rready(lrr),
 .video_status(32'h12345678),.config_status(32'h00080003),.video_complete(32'd11),.video_reject(32'd0),.video_repeat(32'd0),
 .video_supersede(32'd0),.video_underflow(32'd0),.video_errors(32'd0),.video_seq(32'd17),
 .frame_start(1'b0),.pixel_x(12'd0),.pixel_y(12'd0),.in_de(1'b0),.in_hs(1'b0),.in_vs(1'b0),.in_rgb(24'd0),
 .out_de(),.out_hs(),.out_vs(),.out_rgb(),.active_cycle_id(),.marker_visible());
 task check(input logic ok,input string why);begin checks++;if(ok!==1'b1)$fatal(1,"EXPORT_CONTROL_FAIL %s",why);end endtask
 task write_reg(input [7:0] address,input [31:0] data,input [3:0] mask,input integer ad,input integer dd,input [1:0] expected);
  begin
   fork
    begin repeat(ad+1)@(negedge c);aw=address;av=1;do @(posedge c);while(!ard);@(negedge c);av=0;end
    begin repeat(dd+1)@(negedge c);wd=data;st=mask;wv=1;do @(posedge c);while(!wrd);@(negedge c);wv=0;end
   join
   wait(bv);@(negedge c);check(bp==expected,"B响应符合合同");
   repeat(5)begin @(posedge c);check(bv&&bp==expected&&!ard&&!wrd,"B反压期间保持且不接受新写入");end
   @(negedge c);br=1;@(negedge c);br=0;
  end
 endtask
 task read_reg(input [7:0] address,input [1:0] expected);
  reg [31:0] saved;
  begin
   @(negedge c);ar=address;arv=1;do @(posedge c);while(!arr);@(negedge c);arv=0;
   wait(rv);@(negedge c);saved=rd;check(rp==expected,"R响应符合合同");
   repeat(5)begin @(posedge c);check(rv&&rp==expected&&rd===saved&&!arr,"R反压期间数据保持");end
   value=saved;@(negedge c);rr=1;@(negedge c);rr=0;
  end
 endtask
 task produce(input integer id);
  begin @(negedge v);check(available,"存在可写槽");fb=allocation;allocate=1;@(negedge v);allocate=0;
   fs=id;finish=1;@(negedge v);finish=0;end
 endtask
 always @(negedge v)if(!reset&&held)check(!free_mask[bank],"跨域控制不能释放正在复制的槽");
 initial begin
  #300;reset=0;#1000;
  read_reg(0,0);check(value==32'h4d415631,"旧ID可读");
  write_reg(8'h10,32'habc,15,4,0,0);read_reg(8'h10,0);check(value==32'habc,"旧写通路支持W先到");
  read_reg(8'ha0,0);check(value==32'h00080003,"旧配置页保留");
  read_reg(8'hb8,0);check(value==32'h4d565831,"新合同签名");
  read_reg(8'hcc,0);check(value==768000,"帧字节数");read_reg(8'hd0,0);check(value==32'h01e00320,"尺寸合同");
  write_reg(8'hb0,1,15,0,7,0);read_reg(8'hbc,0);check(value==32'h00010000,"无完整帧通过状态报告而非总线故障");
  produce(41);write_reg(8'hb0,1,15,6,0,0);check(held&&seq==41,"借用完整帧");saved_bank=bank;saved_token=token;
  read_reg(8'hc0,0);check(value==saved_token,"令牌原子快照");
  produce(42);produce(43);read_reg(8'hc4,0);check(value==41,"来源更新不改变借用描述符");
  read_reg(8'hc8,0);check(value==32'h10000000+saved_bank*32'hc0000,"地址属于借用槽");
  write_reg(8'hb4,saved_token+1,15,0,3,0);read_reg(8'hbc,0);check(value==32'h00020001&&held,"错误令牌不能释放");
  write_reg(8'hb0,1,15,0,0,0);read_reg(8'hbc,0);check(value==32'h00040001,"不可重复借用");
  write_reg(8'hb4,saved_token,15,3,0,0);check(!held,"正确令牌归还");
  write_reg(8'hb0,1,3,0,0,2);check(!held,"部分写不执行借用");read_reg(8'hb9,2);
  for(identity=44;identity<60;identity++)begin
   produce(identity);write_reg(8'hb0,1,15,identity%4,(identity+1)%4,0);check(held&&seq==identity,"连续会话借用无错帧");
   saved_token=token;read_reg(8'hc4,0);check(value==identity,"连续帧号一致");
   write_reg(8'hb4,saved_token,15,(identity+2)%5,identity%3,0);check(!held,"连续租约全部归还");
  end
  // 命令跨域在途时共同复位，不得留下迟到的响应或句柄。
  @(negedge c);aw=8'hb0;wd=1;av=1;wv=1;do @(posedge c);while(!(ard&&wrd));
  @(negedge c);av=0;wv=0;reset=1;#300;reset=0;#1000;
  check(!bv&&!rv&&!held,"共同复位清空事务和租约");read_reg(8'hd8,0);check(value==0,"共同复位重置完成计数");
  $display("VIDEO_EXPORT_CONTROL_PASS checks=%0d",checks);$finish;
 end
 initial begin #1000000;$fatal(1,"export control timeout");end
endmodule
