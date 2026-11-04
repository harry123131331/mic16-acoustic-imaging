`timescale 1ns/1ps
// 完整帧功能抽象；另一个冒烟测试使用实际生成的PLLE2模型。
// 不覆盖抖动、模拟锁定或时序验收。
// 生成IP参数：输入50MHz，倍频36、预分频1，输出分频18/55/75。
module pll(output clk_out1,output clk_out2,output clk_out3,input resetn,output reg locked,input clk_in1);
 reg c100=0,cpix=0,c24=0;integer lock_edges=0;
 realtime t100=0,tpix=0,t24=0;
 assign clk_out1=resetn&&c100;assign clk_out2=resetn&&cpix;assign clk_out3=resetn&&c24;
 initial begin locked=0;forever begin t100=t100+5.0;#(t100-$realtime);c100=~c100;end end
 initial forever begin tpix=tpix+55.0/3.6;#(tpix-$realtime);cpix=~cpix;end
 initial forever begin t24=t24+75.0/3.6;#(t24-$realtime);c24=~c24;end
 always @(posedge clk_in1 or negedge resetn)begin
  if(!resetn)begin locked<=0;lock_edges<=0;end
  else if(lock_edges<16)lock_edges<=lock_edges+1;
  else locked<=1;
 end
endmodule
