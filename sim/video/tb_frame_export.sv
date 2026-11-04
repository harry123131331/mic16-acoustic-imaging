`timescale 1ns/1ps
module tb_frame_export;
 reg clk=0,reset=1;always #5 clk=~clk;
 reg allocate=0,finish=0,good=1,switch_display=0,acquire=0,release_valid=0;
 reg [1:0] finish_bank=0;reg [31:0] finish_seq=0,release_token=0;
 wire available,ready,held,accepted,exhausted;wire [1:0] allocation,bank;wire [31:0] seq,token;
 wire [2:0] free_mask;wire dv,rv;wire [1:0] db,rb;wire [31:0] ds,rs;
 integer checks=0,leases=0,frames=0,seed=20261020,i;
 reg writer=0;reg [1:0] wb;reg [31:0] saved_token;reg [1:0] saved_bank;
 mic16_frame_banks_export dut(.clk(clk),.reset(reset),.allocate(allocate),.available(available),.allocation(allocation),
 .finish(finish),.finish_good(good),.finish_bank(finish_bank),.finish_seq(finish_seq),
 .switch_display(switch_display),.next_display_valid(),.next_display_bank(),.next_display_seq(),
 .display_valid(dv),.display_bank(db),.display_seq(ds),.ready_valid(rv),.ready_bank(rb),.ready_seq(rs),
 .free_mask(free_mask),.superseded(),.acquire_valid(acquire),.acquire_ready(ready),
 .export_valid(held),.export_bank(bank),.export_seq(seq),.export_token(token),
 .release_valid(release_valid),.release_token(release_token),.release_accepted(accepted),.token_exhausted(exhausted));
 task check(input logic ok,input string why);begin checks++;if(ok!==1'b1)$fatal(1,"FRAME_EXPORT_FAIL %s",why);end endtask
 task tick;begin @(posedge clk);#1;@(negedge clk);end endtask
 task clear;begin allocate=0;finish=0;switch_display=0;acquire=0;release_valid=0;end endtask
 task produce(input integer identity);begin
  check(available,"至少一个可写bank");wb=allocation;allocate=1;tick();allocate=0;
  check(!free_mask[wb],"分配后不可重复分配");finish_bank=wb;finish_seq=identity;finish=1;good=1;tick();finish=0;frames++;
 end endtask
 always @(negedge clk)if(!reset)begin
  if(held)begin check(bank<3&&!free_mask[bank],"导出租约保持不可写");check(!ready,"单个导出读者不能重复借用");end
  if(dv)check(!free_mask[db],"HDMI持有bank不可写");
  if(rv)check(!free_mask[rb],"READY持有bank不可写");
 end
 initial begin
  tick();tick();reset=0;tick();check(!held&&!ready,"无完整图像不可借用");
  produce(10);acquire=1;tick();acquire=0;check(held&&seq==10&&token==1,"获得首个完整帧");leases++;
  saved_bank=bank;saved_token=token;produce(11);produce(12);
  check(held&&bank==saved_bank&&seq==10,"相机完成/替换READY不能改变导出描述符");
  switch_display=1;tick();switch_display=0;check(ds==12&&!free_mask[saved_bank],"HDMI切换仍保留CPU读者");
  release_valid=1;release_token=saved_token+9;tick();release_valid=0;
  check(held&&token==saved_token,"错误旧令牌不能归还当前帧");
  release_valid=1;release_token=saved_token;tick();release_valid=0;
  check(!held&&free_mask[saved_bank],"复制完成后归还非显示bank");
  acquire=1;tick();acquire=0;check(held&&seq==12&&token==2,"可借用显示中的完整帧");leases++;
  release_valid=1;release_token=1;tick();release_valid=0;check(held,"上一租约延迟归还被拒绝");
  saved_bank=bank;release_valid=1;release_token=token;tick();release_valid=0;
  check(!held&&!free_mask[saved_bank],"归还导出不能释放HDMI引用");
  produce(13);saved_bank=rb;acquire=1;switch_display=1;tick();clear();
  check(held&&bank==saved_bank&&db==saved_bank,"同拍借用与显示切换保持一致");leases++;
  produce(14);release_valid=1;release_token=token;switch_display=1;tick();clear();
  check(!held&&free_mask[saved_bank],"同拍释放与切帧正确归还");
  // 同拍新完成替换旧READY，借用锁住的是请求前已经完整的那幅帧。
  produce(15);saved_bank=rb;check(available,"并发场景有空闲bank");wb=allocation;allocate=1;tick();allocate=0;
  finish_bank=wb;finish_seq=16;finish=1;acquire=1;tick();clear();
  check(held&&seq==15&&bank==saved_bank&&rs==16&&!free_mask[saved_bank],"完成/借用同拍不串帧");leases++;
  release_valid=1;release_token=token;tick();clear();
  // 有效生产者、HDMI与延迟读者随机交错，不覆盖仍被借用的bank。
  writer=0;
  for(i=0;i<2000;i++)begin
   clear();
   switch_display=($random(seed)&3)==0;
   acquire=($random(seed)&7)==0;
   if(held&&($random(seed)&7)==0)begin release_valid=1;release_token=($random(seed)&1)?token:token+123;end
   if(writer)begin
    if(($random(seed)&3)==0)begin finish=1;finish_bank=wb;finish_seq=100+i;good=($random(seed)&15)!=0;writer=0;frames++;end
   end else if(available&&($random(seed)&1))begin
    check(!held||allocation!=bank,"新相机写入不借用导出bank");wb=allocation;allocate=1;writer=1;
   end
   if(acquire&&ready)leases++;
   tick();
  end
  clear();reset=1;tick();tick();check(!held&&free_mask==7,"共同复位清空租约与引用");reset=0;tick();
  check(!ready&&!dv&&!rv,"复位后不发布旧描述符");
  $display("FRAME_EXPORT_PASS checks=%0d leases=%0d frames=%0d",checks,leases,frames);$finish;
 end
 initial begin #1000000;$fatal(1,"frame export timeout");end
endmodule
