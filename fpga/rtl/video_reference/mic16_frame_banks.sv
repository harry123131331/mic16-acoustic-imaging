`timescale 1ns/1ps
// 全部帧所有权在AXI域管理，完成意味着末次BRESP与相机帧结束均已到达。
// 仅前一帧读事务全部排空后才允许切换显示帧。
module mic16_frame_banks(
 input wire clk,input wire reset,
 input wire allocate,output reg available,output reg [1:0] allocation,
 input wire finish,input wire finish_good,input wire [1:0] finish_bank,input wire [31:0] finish_seq,
 input wire switch_display,output wire next_display_valid,output wire [1:0] next_display_bank,
 output wire [31:0] next_display_seq,
 output reg display_valid,output reg [1:0] display_bank,output reg [31:0] display_seq,
 output reg ready_valid,output reg [1:0] ready_bank,output reg [31:0] ready_seq,
 output reg [2:0] free_mask,output reg [31:0] superseded
);
 reg [2:0] nf;reg ndv,nrv;reg [1:0] nd,nr;reg [31:0] nds,nrs,ns;
 assign next_display_valid=ready_valid||display_valid;
 assign next_display_bank=ready_valid?ready_bank:display_bank;
 assign next_display_seq=ready_valid?ready_seq:display_seq;
 always @* begin
  available=|free_mask;allocation=0;
  if(free_mask[0])allocation=0;else if(free_mask[1])allocation=1;else allocation=2;
  nf=free_mask;ndv=display_valid;nd=display_bank;nds=display_seq;
  nrv=ready_valid;nr=ready_bank;nrs=ready_seq;ns=superseded;
  // 先消费已有READY；同拍刚完成的帧等待下一帧显示。
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
 end
 always @(posedge clk)begin
  if(reset)begin free_mask<=3'b111;display_valid<=0;display_bank<=0;display_seq<=0;
   ready_valid<=0;ready_bank<=0;ready_seq<=0;superseded<=0;end
  else begin free_mask<=nf;display_valid<=ndv;display_bank<=nd;display_seq<=nds;
   ready_valid<=nrv;ready_bank<=nr;ready_seq<=nrs;superseded<=ns;end
 end
endmodule
