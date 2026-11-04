`timescale 1ns/1ps
// 复用原相机/DDR/HDMI故障回归，同时验证真实帧存储器中的导出引用。
module tb_frame_store_export;
 tb_frame_ownership test();
 defparam test.dut.ENABLE_EXPORT=1;
 reg acquire=0,release_valid=0;reg [31:0] release_token=0;
 reg [63:0] saved[0:127];integer checks=0,i,bank;reg [31:0] token,sequence_id;
 task check(input logic ok,input string why);begin checks++;if(ok!==1'b1)$fatal(1,"STORE_EXPORT_FAIL %s",why);end endtask
 initial begin
  force test.dut.acquire_valid=acquire;
  force test.dut.release_valid=release_valid;
  force test.dut.release_token=release_token;
  wait(!test.reset);wait(test.completed>=2);@(negedge test.clk);
  acquire=1;wait(test.dut.export_valid);@(negedge test.clk);acquire=0;
  bank=test.dut.export_bank;token=test.dut.export_token;sequence_id=test.dut.export_seq;
  for(i=0;i<128;i++)saved[i]=test.memory[bank*512+i];
  repeat(8000)begin
   @(negedge test.clk);check(test.dut.export_valid&&test.dut.export_bank==bank&&test.dut.export_seq==sequence_id,"相机/HDMI运行时导出描述符稳定");
   if(test.awvalid)check((test.awaddr-32'h10000000)/4096!=bank,"AXI写地址不指向导出槽");
   check(test.memory[bank*512+(checks%128)]===saved[checks%128],"借用期间DDR内容未被重写");
  end
  release_token=token+99;release_valid=1;@(negedge test.clk);release_valid=0;
  check(test.dut.export_valid,"错误令牌不释放DDR引用");
  release_token=token;release_valid=1;@(negedge test.clk);release_valid=0;
  check(!test.dut.export_valid,"正确令牌归还DDR引用");
  $display("FRAME_STORE_EXPORT_PASS checks=%0d",checks);
 end
endmodule
