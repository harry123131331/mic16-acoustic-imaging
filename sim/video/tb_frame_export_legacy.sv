`timescale 1ns/1ps
module tb_frame_export_legacy;
 reg clk=0,reset=1;always #5 clk=~clk;
 reg allocate=0,finish=0,good=1,sw=0;reg [1:0] fb=0,wb=0;reg [31:0] fs=0;
 wire a,b,ndv1,ndv2,dv1,dv2,rv1,rv2;wire [1:0] ab,bb,nd1,nd2,db1,db2,rb1,rb2;
 wire [31:0] ns1,ns2,ds1,ds2,rs1,rs2,sp1,sp2;wire [2:0] free1,free2;
 integer i,checks=0,seed=20261020;reg writing=0;
 mic16_frame_banks old(.clk(clk),.reset(reset),.allocate(allocate),.available(a),.allocation(ab),.finish(finish),.finish_good(good),.finish_bank(fb),.finish_seq(fs),
 .switch_display(sw),.next_display_valid(ndv1),.next_display_bank(nd1),.next_display_seq(ns1),.display_valid(dv1),.display_bank(db1),.display_seq(ds1),
 .ready_valid(rv1),.ready_bank(rb1),.ready_seq(rs1),.free_mask(free1),.superseded(sp1));
 mic16_frame_banks_export #(.ENABLE_EXPORT(0)) candidate(.clk(clk),.reset(reset),.allocate(allocate),.available(b),.allocation(bb),.finish(finish),.finish_good(good),.finish_bank(fb),.finish_seq(fs),
 .switch_display(sw),.next_display_valid(ndv2),.next_display_bank(nd2),.next_display_seq(ns2),.display_valid(dv2),.display_bank(db2),.display_seq(ds2),
 .ready_valid(rv2),.ready_bank(rb2),.ready_seq(rs2),.free_mask(free2),.superseded(sp2),
 .acquire_valid(1'bx),.acquire_ready(),.export_valid(),.export_bank(),.export_seq(),.export_token(),
 .release_valid(1'bx),.release_token(32'hxxxxxxxx),.release_accepted(),.token_exhausted());
 initial begin
  repeat(3)@(negedge clk);reset=0;
  for(i=0;i<3000;i++)begin
   allocate=0;finish=0;sw=($random(seed)&3)==0;
   if(writing)begin if(($random(seed)&3)==0)begin finish=1;fb=wb;fs=i;good=($random(seed)&15)!=0;writing=0;end end
   else if(a&&($random(seed)&1))begin allocate=1;wb=ab;writing=1;end
   @(posedge clk);#1;checks++;
   if({a,ab,ndv1,nd1,ns1,dv1,db1,ds1,rv1,rb1,rs1,free1,sp1}!=={b,bb,ndv2,nd2,ns2,dv2,db2,ds2,rv2,rb2,rs2,free2,sp2})$fatal(1,"LEGACY_EXPORT_DISABLED_MISMATCH %0d",i);
   @(negedge clk);
  end
  $display("FRAME_EXPORT_LEGACY_PASS checks=%0d",checks);$finish;
 end
endmodule
