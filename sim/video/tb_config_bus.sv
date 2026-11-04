`timescale 1ns/1ps
// 目标译码器仅由SCL/SDA引脚驱动，不读设计内部ACK或ROM。
module config_target #(parameter CAMERA=1, ENTRIES=252, BYTES=4)(
 input wire resetn,input wire scl,inout wire sda,input integer mode,
 output reg reset_request,output integer accepted,output integer epoch_accepted,
 output integer attempts,output integer nacked,output integer aborted,
 output integer byte_checks,output integer ack_checks,
 output reg [63:0] min_low,output reg [63:0] min_high,output reg [63:0] min_period);
 reg drive_low=0,active=0,failed=0;
 reg [23:0] expected_rom[0:251];
 reg [7:0] shifted=0;
 integer byte_index=0,bit_index=0,phase=0,index=0,try_index=0;
 integer sweep_attempts=0,absent_attempts=0;
 reg chosen_nack=0;
 real rise_time=0,fall_time=0,last_rise=0;
 // 解析后的ACK驱动检测高电平争用，NACK场景检测错误拉低。
 assign (strong0,highz1) sda=drive_low?1'b0:1'bz;
 task require_ok(input logic yes,input string message);
  if(yes!==1'b1)$fatal(1,"CONFIG_FAIL camera=%0d index=%0d byte=%0d bit=%0d %s time=%t",CAMERA,index,byte_index,bit_index,message,$time);
 endtask
 function automatic [7:0] expected_byte(input integer n);
  if(n==0)expected_byte=CAMERA?8'h78:8'h72;
  else if(CAMERA)case(n)1:expected_byte=expected_rom[index][23:16];2:expected_byte=expected_rom[index][15:8];3:expected_byte=expected_rom[index][7:0];endcase
  else expected_byte=(n==1)?expected_rom[index][15:8]:expected_rom[index][7:0];
 endfunction
 initial begin
  reset_request=0;accepted=0;epoch_accepted=0;attempts=0;nacked=0;aborted=0;
  byte_checks=0;ack_checks=0;min_low=64'hffffffffffffffff;min_high=min_low;min_period=min_low;
  if(CAMERA)$readmemh("camera_expected.hex",expected_rom,0,ENTRIES-1);
  else $readmemh("hdmi_expected.hex",expected_rom,0,ENTRIES-1);
 end
 always @(negedge resetn)begin
  if(active)aborted++;
  active=0;drive_low=0;index=0;epoch_accepted=0;byte_index=0;bit_index=0;phase=0;
  shifted=0;failed=0;rise_time=0;fall_time=0;last_rise=0;
 end
 always @(negedge sda)if(resetn&&scl===1'b1)begin
  require_ok(!active,"unexpected repeated START in register write");
  require_ok(index<ENTRIES,"transaction after full table");
  active=1;byte_index=0;bit_index=0;phase=0;shifted=0;failed=0;drive_low=0;
  try_index=(mode==1&&index==2)?sweep_attempts:0;
  attempts++;
 end
 always @(posedge scl)if(resetn)begin
  if(active&&byte_index<BYTES)begin
   if(phase==0)begin
    if(fall_time>0&&$realtime-fall_time<min_low)min_low=$realtime-fall_time;
    if(last_rise>0&&$realtime-last_rise<min_period)min_period=$realtime-last_rise;
    last_rise=$realtime;
    require_ok(sda===1'b0||sda===1'b1,"nonbinary data bit");
    shifted={shifted[6:0],sda};bit_index++;
    if(mode==3&&!reset_request&&index==2&&byte_index==1&&bit_index==4)reset_request=1;
    if(bit_index==8)begin
     byte_checks++;require_ok(shifted===expected_byte(byte_index),$sformatf("serialized byte actual=%h expected=%h",shifted,expected_byte(byte_index)));
     phase=1;
    end
   end else if(phase==2)begin
    ack_checks++;require_ok(sda===(chosen_nack?1'b1:1'b0),"ACK/NACK pin value or release mismatch");
    failed=failed||chosen_nack;byte_index++;bit_index=0;shifted=0;phase=3;
   end else require_ok(0,"clock edge before ACK model setup");
  end
  rise_time=$realtime;
 end
 always @(negedge scl)if(resetn)begin
  if(active&&rise_time>0&&$realtime-rise_time<min_high)min_high=$realtime-rise_time;
  fall_time=$realtime;
  if(active&&phase==1)begin
   chosen_nack=(mode==1&&index==2&&try_index<BYTES&&byte_index==try_index)||
               (mode==2&&absent_attempts<3);
   drive_low=!chosen_nack;phase=2;
  end else if(active&&phase==3)begin drive_low=0;phase=0;end
 end
 always @(posedge sda)if(resetn&&scl===1'b1&&active)begin
  require_ok(byte_index==BYTES&&bit_index==0&&phase==0,"STOP before complete byte/ACK sequence");
  drive_low=0;active=0;
  if(mode==1&&index==2)sweep_attempts++;
  if(mode==2&&absent_attempts<3)absent_attempts++;
  if(failed)nacked++;
  else begin accepted++;epoch_accepted++;index++;end
  if(index<=3||failed||index==ENTRIES)$display("CONFIG_TX camera=%0d mode=%0d index_after=%0d failed=%0d attempts=%0d time_ns=%0f",CAMERA,mode,index,failed,attempts,$realtime);
 end
endmodule

module config_case #(parameter CAMERA=1)(input integer mode,output reg finished);
 reg clk=0,resetn=0,go=0;
 always #10 clk=~clk;
 wire scl,done,reset_request;
 tri1 sda;
 wire [31:0] accepted,epoch_accepted,attempts,nacked,aborted,byte_checks,ack_checks;
 wire [63:0] min_low,min_high,min_period;
 generate if(CAMERA)begin
  camera_init #(.CAMERA_TYPE("ov5640"),.IMAGE_TYPE(0),.IMAGE_WIDTH(800),.IMAGE_HEIGHT(480),.IMAGE_FLIP_EN(0),.IMAGE_MIRROR_EN(0)) dut(
   .Clk(clk),.Rst_n(resetn),.Init_Done(done),.camera_rst_n(),.camera_pwdn(),.i2c_sclk(scl),.i2c_sdat(sda));
 end else begin
  SiI9022_Init dut(.Clk(clk),.Rst_n(resetn),.Go(go),.device_id(16'h72),.Init_Done(done),.i2c_sclk(scl),.i2c_sdat(sda));
 end endgenerate
 config_target #(.CAMERA(CAMERA),.ENTRIES(CAMERA?252:42),.BYTES(CAMERA?4:3)) target(
  .resetn(resetn),.scl(scl),.sda(sda),.mode(mode),.reset_request(reset_request),
  .accepted(accepted),.epoch_accepted(epoch_accepted),.attempts(attempts),.nacked(nacked),.aborted(aborted),
  .byte_checks(byte_checks),.ack_checks(ack_checks),.min_low(min_low),.min_high(min_high),.min_period(min_period));
 always @(posedge done)if(resetn)begin
  if(epoch_accepted!=(CAMERA?252:42))$fatal(1,"CONFIG_FAIL premature Init_Done camera=%0d accepted=%0d",CAMERA,epoch_accepted);
 end
 initial begin
  finished=0;
  #100;resetn=1;repeat(20)@(negedge clk);go=1;@(negedge clk);go=0;
  if(mode==3)begin
   wait(reset_request);@(negedge clk);resetn=0;#200;
   if(done!==0)$fatal(1,"CONFIG_FAIL Init_Done not cleared at reset");
   resetn=1;repeat(20)@(negedge clk);go=1;@(negedge clk);go=0;
  end
  wait(done);repeat(100)@(negedge clk);
  if(epoch_accepted!=(CAMERA?252:42))$fatal(1,"CONFIG_FAIL incomplete table");
  if(mode==1&&nacked!=(CAMERA?4:3))$fatal(1,"CONFIG_FAIL NACK sweep coverage");
  if(mode==2&&nacked!=3)$fatal(1,"CONFIG_FAIL absence coverage");
  if(mode==3&&aborted!=1)$fatal(1,"CONFIG_FAIL midbyte reset coverage");
  $display("CONFIG_CASE_PASS camera=%0d mode=%0d accepted=%0d epoch=%0d attempts=%0d nacked=%0d aborted=%0d bytes=%0d ack=%0d low_ns=%0d high_ns=%0d period_ns=%0d",CAMERA,mode,accepted,epoch_accepted,attempts,nacked,aborted,byte_checks,ack_checks,min_low,min_high,min_period);
  finished=1;
 end
endmodule

module tb_config_bus;
 integer mode=0;
 wire camera_finished,hdmi_finished;
 initial if(!$value$plusargs("MODE%d",mode))mode=0;
 config_case #(.CAMERA(1)) cam(mode,camera_finished);
 config_case #(.CAMERA(0)) hdmi(mode,hdmi_finished);
 initial begin wait(camera_finished&&hdmi_finished);$display("CONFIG_BUS_FUNCTIONAL_PASS mode=%0d",mode);$finish;end
 initial begin #120000000;$fatal(1,"CONFIG_FAIL timeout mode=%0d camera=%0d hdmi=%0d",mode,camera_finished,hdmi_finished);end
endmodule
