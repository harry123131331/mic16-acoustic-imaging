`timescale 1ns/1ps
// b0及以上为导出子页；低地址原样转发。每个方向各允许一个未完成事务。
// AW/W分别锁存；请求和完整回复经XPM队列跨域，不异步读取多位描述符。
module mic16_video_export_regs #(
 parameter [31:0] BASE=32'h10000000,STRIDE=32'hc0000,
 parameter integer WIDTH=800,HEIGHT=480
)(
 input wire reset,input wire ctrl_clk,input wire video_clk,
 input wire [7:0] awaddr,input wire awvalid,output wire awready,
 input wire [31:0] wdata,input wire [3:0] wstrb,input wire wvalid,output wire wready,
 output reg [1:0] bresp,output reg bvalid,input wire bready,
 input wire [7:0] araddr,input wire arvalid,output wire arready,
 output reg [31:0] rdata,output reg [1:0] rresp,output reg rvalid,input wire rready,
 output wire [7:0] legacy_awaddr,output wire legacy_awvalid,input wire legacy_awready,
 output wire [31:0] legacy_wdata,output wire [3:0] legacy_wstrb,output wire legacy_wvalid,input wire legacy_wready,
 input wire [1:0] legacy_bresp,input wire legacy_bvalid,output wire legacy_bready,
 output wire [7:0] legacy_araddr,output wire legacy_arvalid,input wire legacy_arready,
 input wire [31:0] legacy_rdata,input wire [1:0] legacy_rresp,input wire legacy_rvalid,output wire legacy_rready,
 output wire acquire_valid,input wire acquire_ready,input wire export_valid,
 input wire [1:0] export_bank,input wire [31:0] export_seq,input wire [31:0] export_token,
 output wire release_valid,output wire [31:0] release_token,input wire release_accepted,input wire token_exhausted
);
 wire cr,vr;
 xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) c_reset(.src_arst(reset),.dest_clk(ctrl_clk),.dest_arst(cr));
 xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) v_reset(.src_arst(reset),.dest_clk(video_clk),.dest_arst(vr));
 reg ah,wh;reg [7:0] wa;reg [31:0] wd;reg [3:0] strobe;
 reg [1:0] ws;reg as_sent,ds_sent;
 reg read_pending,read_sent;reg [7:0] ra;
 reg [31:0] cached_status,cached_token,cached_seq,cached_address,completion;
 wire req_ready,req_valid,reply_ready,reply_valid;wire [32:0] request;wire [127:0] reply;
 reg response_pending,video_wait;reg [15:0] reason;reg [127:0] response;
 assign awready=!cr&&!ah&&(ws==0)&&!bvalid;
 assign wready=!cr&&!wh&&(ws==0)&&!bvalid;
 assign arready=!cr&&!read_pending&&!rvalid;
 assign legacy_awaddr=wa;assign legacy_wdata=wd;assign legacy_wstrb=strobe;
 assign legacy_awvalid=(ws==1)&&!as_sent;assign legacy_wvalid=(ws==1)&&!ds_sent;
 assign legacy_bready=(ws==1)&&!bvalid;
 assign legacy_araddr=ra;assign legacy_arvalid=read_pending&&!read_sent;
 assign legacy_rready=read_pending&&read_sent&&!rvalid;
 // b0写1借用；b4写令牌归还。B完成表示命令已执行，操作结果须检查STATUS。
 mic16_video_queue #(.WIDTH(33)) commands(reset,ctrl_clk,video_clk,{wa==8'hb4,wd},ws==2,req_ready,
   request,req_valid,!video_wait&&!response_pending);
 wire execute=req_valid&&!video_wait&&!response_pending&&!vr;
 assign acquire_valid=execute&&!request[32]&&acquire_ready;
 assign release_valid=execute&&request[32];assign release_token=request[31:0];
 mic16_video_queue #(.WIDTH(128)) responses(reset,video_clk,ctrl_clk,response,response_pending,reply_ready,
   reply,reply_valid,ws==3&&!bvalid);
 always @(posedge video_clk)begin
  if(vr)begin response_pending<=0;video_wait<=0;reason<=0;response<=0;end
  else begin
   if(response_pending&&reply_ready)response_pending<=0;
   if(execute)begin
    video_wait<=1;
    if(request[32])reason<=release_accepted?0:16'd2;
    else reason<=acquire_ready?0:(token_exhausted?16'd3:(export_valid?16'd4:16'd1));
   end
   if(video_wait)begin
    // 比银行所有权更新晚一拍，整组描述符从同一状态采样。
    response<={BASE+export_bank*STRIDE,export_seq,export_token,reason,13'd0,token_exhausted,(reason==0),export_valid};
    response_pending<=1;video_wait<=0;
   end
  end
 end
 always @(posedge ctrl_clk)begin
  if(cr)begin
   ah<=0;wh<=0;wa<=0;wd<=0;strobe<=0;ws<=0;as_sent<=0;ds_sent<=0;bresp<=0;bvalid<=0;
   read_pending<=0;read_sent<=0;ra<=0;rdata<=0;rresp<=0;rvalid<=0;
   cached_status<=0;cached_token<=0;cached_seq<=0;cached_address<=0;completion<=0;
  end else begin
   if(bvalid&&bready)bvalid<=0;
   if(rvalid&&rready)rvalid<=0;
   if(awvalid&&awready)begin ah<=1;wa<=awaddr;end
   if(wvalid&&wready)begin wh<=1;wd<=wdata;strobe<=wstrb;end
   if(ws==0&&ah&&wh&&!bvalid)begin
    ah<=0;wh<=0;as_sent<=0;ds_sent<=0;
    if(wa<8'hb0)ws<=1;
    else if(strobe==15&&((wa==8'hb0&&wd==1)||wa==8'hb4))ws<=2;
    else begin bvalid<=1;bresp<=2;end
   end
   if(ws==1)begin
    if(legacy_awvalid&&legacy_awready)as_sent<=1;
    if(legacy_wvalid&&legacy_wready)ds_sent<=1;
    if(legacy_bvalid&&legacy_bready)begin bvalid<=1;bresp<=legacy_bresp;ws<=0;end
   end
   if(ws==2&&req_ready)ws<=3;
   if(ws==3&&reply_valid&&!bvalid)begin
    cached_status<=reply[31:0];cached_token<=reply[63:32];cached_seq<=reply[95:64];cached_address<=reply[127:96];
    completion<=completion+1;bvalid<=1;bresp<=0;ws<=0;
   end
   if(arvalid&&arready)begin
    if(araddr<8'hb0)begin ra<=araddr;read_pending<=1;read_sent<=0;end
    else begin
     rvalid<=1;rdata<=0;rresp<=0;
     case(araddr)
      8'hb8:rdata<=32'h4d565831;
      8'hbc:rdata<=cached_status;
      8'hc0:rdata<=cached_token;
      8'hc4:rdata<=cached_seq;
      8'hc8:rdata<=cached_address;
      8'hcc:rdata<=WIDTH*HEIGHT*2;
      8'hd0:rdata<=(HEIGHT<<16)|WIDTH;
      8'hd4:rdata<=1;
      8'hd8:rdata<=completion;
      default:rresp<=2;
     endcase
    end
   end
   if(legacy_arvalid&&legacy_arready)read_sent<=1;
   if(legacy_rvalid&&legacy_rready)begin
    rdata<=legacy_rdata;rresp<=legacy_rresp;rvalid<=1;read_pending<=0;read_sent<=0;
   end
  end
 end
endmodule
