`timescale 1ns/1ps
// 实际打包器、PDM时钟模块和原厂concat共同验证状态位顺序及粘滞溢出。
module tb_audio_capture_status;
 reg clk=0,rst_n=0,enable=0,ready=1;
 reg [511:0] flat=0;reg [15:0] valids=0;
 wire [31:0] data,overflows;wire [3:0] keep;wire last,valid,overflow,running;
 reg [1:0] buttons=2'b01;
 wire [3:0] gpio_word;wire [1:0] status=gpio_word[3:2];wire [15:0] level;wire pdm_clock,tick;
 integer checks=0,beats=0,packets=0,phase=0,s,c;
 always #5 clk=~clk;
 pdm_clk_gen clock_source(.clk_ref(clk),.rst_n(rst_n),.enable(enable),.pdm_clk_div(16'd20),
  .pdm_clk(pdm_clock),.pdm_sample_tick(tick),.pdm_clk_running(running));
 pdm_axis_pack_nch dut(.clk(clk),.rst_n(rst_n),.enable(enable),.pcm_samples_flat(flat),.pcm_valids(valids),
  .m_axis_tdata(data),.m_axis_tkeep(keep),.m_axis_tlast(last),.m_axis_tvalid(valid),.m_axis_tready(ready),
  .dbg_channel(),.dbg_sample_index(),.overflow_error(overflow),.dbg_fifo_level(level),.overflow_count(overflows));
 xlconcat_v2_1_4_xlconcat #(.NUM_PORTS(3),.IN0_WIDTH(2),.dout_width(4)) status_concat(.In0(buttons),.In1(overflow),.In2(running),.dout(gpio_word));
 task check(input bit ok,input [255:0] label_text);
  begin checks=checks+1;if(!ok)$fatal(1,"STATUS_FAIL %s beat=%0d",label_text,beats);end
 endtask
 task sample_group(input integer index);
  begin
   @(negedge clk);for(c=0;c<16;c=c+1)flat[c*32+:32]=32'hbeef0000+index*16+c;
   valids=16'hffff;@(negedge clk);valids=0;repeat(80)@(negedge clk);
  end
 endtask
 always @(posedge clk)begin
  if(rst_n && enable && phase==0 && valid && ready)begin
   check(data==32'hbeef0000+beats,"channel_and_sample_order");check(keep==15,"keep");
   check(last==(beats%256==255),"packet_exactly_1024_bytes");
   if(last)packets=packets+1;beats=beats+1;
  end
 end
 initial begin
  repeat(4)@(negedge clk);rst_n=1;repeat(3)@(negedge clk);
  check(status==0,"disabled_zero");enable=1;repeat(3)@(negedge clk);check(status==2,"clock_is_bit1");
  for(s=0;s<32;s=s+1)sample_group(s);
  repeat(100)@(negedge clk);check(beats==512 && packets==2 && !overflow,"normal_two_packets");
  phase=1;ready=0;for(s=0;s<16;s=s+1)sample_group(s);
  check(status==3 && overflows>0,"overflow_is_gpio_bit2");check(gpio_word[1:0]==buttons,"buttons_preserved");
  buttons=2'b10;#1;check(gpio_word[1:0]==buttons && status==3,"buttons_independent");ready=1;repeat(800)@(negedge clk);
  check(status==3,"overflow_sticky_after_drain");
  enable=0;repeat(4)@(negedge clk);check(status==0 && overflows==0,"disable_clears_status");
  beats=0;packets=0;phase=0;enable=1;for(s=0;s<16;s=s+1)sample_group(s);
  repeat(100)@(negedge clk);check(beats==256 && packets==1 && status==2,"restart_packet_boundary");
  $display("AUDIO_CAPTURE_STATUS_PASS checks=%0d normal_packets=3 GPIO_BIT2_OVERFLOW_BIT3_CLOCK_BUTTONS_PRESERVED NOT_BOARD",checks);$finish;
 end
 initial begin #1000000;$fatal(1,"STATUS_TIMEOUT");end
endmodule
