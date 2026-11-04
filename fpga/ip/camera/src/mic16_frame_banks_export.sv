`timescale 1ns/1ps
// 增加一个同步导出读者；租约期间相机不可重用该bank，HDMI仍可共享只读。
// common reset须同时停掉外部读者并清空控制队列；复位后旧软件句柄失效。
module mic16_frame_banks_export #(
 parameter integer ENABLE_EXPORT=1
)(
 input wire clk,input wire reset,
 input wire allocate,output reg available,output reg [1:0] allocation,
 input wire finish,input wire finish_good,input wire [1:0] finish_bank,input wire [31:0] finish_seq,
 input wire switch_display,output wire next_display_valid,output wire [1:0] next_display_bank,
 output wire [31:0] next_display_seq,
 output reg display_valid,output reg [1:0] display_bank,output reg [31:0] display_seq,
 output reg ready_valid,output reg [1:0] ready_bank,output reg [31:0] ready_seq,
 output reg [2:0] free_mask,output reg [31:0] superseded,
 input wire acquire_valid,output wire acquire_ready,
 output reg export_valid,output reg [1:0] export_bank,output reg [31:0] export_seq,output reg [31:0] export_token,
 input wire release_valid,input wire [31:0] release_token,output wire release_accepted,
 output wire token_exhausted
);
 reg [2:0] nf;reg ndv,nrv;reg [1:0] nd,nr;reg [31:0] nds,nrs,ns;
 reg nev;reg [1:0] neb;reg [31:0] nes,net,generation;
 wire claim=acquire_valid&&acquire_ready;
 assign acquire_ready=ENABLE_EXPORT&&!reset&&!export_valid&&(ready_valid||display_valid)&&(generation!=32'hffffffff);
 assign release_accepted=ENABLE_EXPORT&&!reset&&release_valid&&export_valid&&(release_token==export_token);
 assign token_exhausted=ENABLE_EXPORT&&(generation==32'hffffffff);
 assign next_display_valid=ready_valid||display_valid;
 assign next_display_bank=ready_valid?ready_bank:display_bank;
 assign next_display_seq=ready_valid?ready_seq:display_seq;
 always @* begin
  available=|free_mask;allocation=0;
  if(free_mask[0])allocation=0;else if(free_mask[1])allocation=1;else allocation=2;
  nf=free_mask;ndv=display_valid;nd=display_bank;nds=display_seq;
  nrv=ready_valid;nr=ready_bank;nrs=ready_seq;ns=superseded;
  nev=export_valid;neb=export_bank;nes=export_seq;net=export_token;
  // 借用本拍开始已有的READY或DISPLAY，不把尚未完成的写帧发布给CPU。
  if(claim)begin nev=1;neb=next_display_bank;nes=next_display_seq;net=generation+1;end
  if(release_accepted)nev=0;
  if(switch_display&&ready_valid)begin
   if(display_valid)nf[display_bank]=1;
   ndv=1;nd=ready_bank;nds=ready_seq;nrv=0;
  end
  if(finish)begin
   if(finish_good)begin
    if(nrv)begin nf[nr]=1;ns=ns+1;end
    nrv=1;nr=finish_bank;nrs=finish_seq;
   end else nf[finish_bank]=1;
  end
  if(allocate&&available)nf[allocation]=0;
  // 归还仅解除导出持有；仍被HDMI或READY引用的bank不可成为空闲。
  if(release_accepted&&!(ndv&&nd==export_bank)&&!(nrv&&nr==export_bank))nf[export_bank]=1;
  // READY被新帧替换或HDMI切帧时，也必须保留导出读者的所有权。
  if(nev)nf[neb]=0;
 end
 always @(posedge clk)begin
  if(reset)begin
   free_mask<=3'b111;display_valid<=0;display_bank<=0;display_seq<=0;
   ready_valid<=0;ready_bank<=0;ready_seq<=0;superseded<=0;
   export_valid<=0;export_bank<=0;export_seq<=0;export_token<=0;generation<=0;
  end else begin
   free_mask<=nf;display_valid<=ndv;display_bank<=nd;display_seq<=nds;
   ready_valid<=nrv;ready_bank<=nr;ready_seq<=nrs;superseded<=ns;
   export_valid<=nev;export_bank<=neb;export_seq<=nes;export_token<=net;
   if(claim)generation<=generation+1;
  end
 end
endmodule
