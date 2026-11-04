`timescale 1ns/1ps
// 当前完整相机路径RTL包含实际PLL、DVP、FIFO、光栅及叠加。
// 仅外部传感器字节、PS-Lite主机及DDR响应器为模型。
module tb_av_system;
 localparam integer N=800*480,STRIDE=32'hc0000;
 localparam [31:0] BASE=32'h10000000;
 reg ref_clk=0,ctrl_clk=0,cam_clk=0,resetn=0;
 always #10 ref_clk=~ref_clk;always #5 ctrl_clk=~ctrl_clk;always #10 cam_clk=~cam_clk;
 reg vsync=1,href=0;reg [7:0] camera_byte=0;tri1 camera_sda,hdmi_sda;
 wire [15:0] rgb;wire de,hs,vs,pix_clk,video_clk,video_resetn;
 reg [7:0] awaddr=0,araddr=0;reg [31:0] wdata=0;reg [3:0] wstrb=15;
 reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
 wire awready,wready,bvalid,arready,rvalid;wire [1:0] bresp,rresp;wire [31:0] rdata;
 wire [31:0] mawaddr,maraddr;wire [7:0] mawlen,marlen,mstrb;
 wire [2:0] mawsize,marsize;wire [1:0] mawburst,marburst;
 wire mawvalid,mwvalid,mwlast,mbready,marvalid,mrready;wire [63:0] mwdata;
 reg wactive=0,ractive=0,mbvalid=0;integer cyc=0,wbase=0,rbase=0,wb=0,rb=0,bdelay=0,rdelay=0;
 reg [63:0] memory[0:3*STRIDE/8-1];
 wire mawready=!wactive&&!mbvalid&&!bdelay&&(cyc%5!=0);
 wire mwready=wactive&&(cyc%4!=0);wire marready=!ractive&&(cyc%7!=0);
 wire mrvalid=ractive&&(rdelay==0),mrlast=(rb==15);wire [63:0] mrdata=memory[rbase+rb];
 integer checks=0,raw_frames=0,full_frames=0,black_frames=0,partial_frames=0,finished_frames=0;
 integer checked_pixels=0,green_pixels=0,held_pixels=0,label_pixels=0,expired_points=0;
 integer image_id=0,last_id=0,repeated_ids=0,index=0,frame_pixels=0,row_pixels=0,rows=0;
 integer valid_overlay_frames=0,cleared_frames=0,expiry_frames=0,ambiguous_marker_pixels=0;
 integer uncalibrated_frames=0,stale_frames=0;
 integer sensor_id=0,camera_done=0,bad_reject_before=0;
 reg previous_de=0,previous_vs=0,frame_black=0;integer previous_vs_tick=0,pixel_tick=0;
 bit forbidden[0:255];reg prev_aw=0,prev_ar=0,prev_w=0;reg [31:0] saved_aw,saved_ar;reg [72:0] saved_w;
 reg [63:0] commands[0:83];integer pending_case=-1,active_case=-1;
 real commit_time=0,active_commit_time=0;
 integer fd=0,saved_frames=0,smoke=0;reg dump_frame=0;reg [15:0] expected,background;integer hit_kind;
 mic16_camera_path dut(
 .reset_n(resetn),.ps2pl_clk50m_0(ref_clk),.ps2pl_resetn_0(resetn),.ctrl_clk(ctrl_clk),.ctrl_resetn(resetn),
 .led(),.camera_sclk(),.camera_sdat(camera_sda),.camera_vsync(vsync),.camera_href(href),.camera_pclk(cam_clk),.camera_xclk(),.camera_data(camera_byte),
 .TFT_rgb(rgb),.TFT_hs(hs),.TFT_vs(vs),.TFT_clk(pix_clk),.TFT_de(de),.TFT_pwm(),.SiI9022_sclk(),.SiI9022_sdat(hdmi_sda),
 .m_video_aclk(video_clk),.m_video_resetn(video_resetn),
 .m_video_awaddr(mawaddr),.m_video_awlen(mawlen),.m_video_awsize(mawsize),.m_video_awburst(mawburst),
 .m_video_awlock(),.m_video_awcache(),.m_video_awprot(),.m_video_awqos(),.m_video_awregion(),.m_video_awvalid(mawvalid),.m_video_awready(mawready),
 .m_video_wdata(mwdata),.m_video_wstrb(mstrb),.m_video_wlast(mwlast),.m_video_wvalid(mwvalid),.m_video_wready(mwready),
 .m_video_bresp(2'd0),.m_video_bvalid(mbvalid),.m_video_bready(mbready),
 .m_video_araddr(maraddr),.m_video_arlen(marlen),.m_video_arsize(marsize),.m_video_arburst(marburst),
 .m_video_arlock(),.m_video_arcache(),.m_video_arprot(),.m_video_arqos(),.m_video_arregion(),.m_video_arvalid(marvalid),.m_video_arready(marready),
 .m_video_rdata(mrdata),.m_video_rresp(2'd0),.m_video_rlast(mrlast),.m_video_rvalid(mrvalid),.m_video_rready(mrready),
 .s_axi_awaddr(awaddr),.s_axi_awprot(3'd0),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
 .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),.s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
 .s_axi_araddr(araddr),.s_axi_arprot(3'd0),.s_axi_arvalid(arvalid),.s_axi_arready(arready),.s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready));
 task check(input logic yes,input string why);begin checks++;if(yes!==1'b1)$fatal(1,"AV_SYSTEM_FAIL %s time=%t output_i=%0d sensor=%0d",why,$time,index,sensor_id);end endtask
 function automatic [15:0] pattern(input integer id,input integer i);pattern=(id*257)^(i*73);endfunction
 // 为样本ID0011和0022独立给出字形行。
 function automatic bit glyph_pixel(input integer digit,input integer row,input integer col);
  reg [2:0] bits;begin
   bits=0;
   if(digit==0)bits=(row==0||row==4)?3'b111:3'b101;
   if(digit==1)case(row)0,2,3:bits=3'b010;1:bits=3'b110;4:bits=3'b111;endcase
   if(digit==2)case(row)0,2,4:bits=3'b111;1:bits=3'b001;3:bits=3'b100;endcase
   glyph_pixel=(col<3)&&bits[2-col];
  end
 endfunction
 // 当前/保持标记分别返回1/2，对应匿名ID字形返回3/4。
 function automatic integer marker_at(input integer x,input integer y);
  integer k,dx,dy,lx,ly,digit,result;begin
   result=0;
   for(k=0;k<2;k++)begin
    dx=x-(k?560:240);dy=y-(k?300:80);lx=dx-8;ly=dy+2;
    if(k==0 && ((dx>=-5&&dx<=5&&dy>=-1&&dy<=1)||(dy>=-5&&dy<=5&&dx>=-1&&dx<=1)))result=1;
    if(k==1 && (((dx==-5||dx==5)&&dy>=-5&&dy<=5)||((dy==-5||dy==5)&&dx>=-5&&dx<=5)))result=2;
    if(result==0 && lx>=0&&lx<16&&ly>=0&&ly<5)begin
     digit=lx/4<2?0:(k?2:1);
     if(glyph_pixel(digit,ly,lx%4))result=k?4:3;
    end
   end
   marker_at=result;
  end
 endfunction
 task write_reg(input [7:0] addr,input [31:0] value);
  begin
   fork
    begin @(negedge ctrl_clk);awaddr=addr;awvalid=1;do @(posedge ctrl_clk);while(!awready);@(negedge ctrl_clk);awvalid=0;end
    begin repeat(2)@(negedge ctrl_clk);wdata=value;wvalid=1;do @(posedge ctrl_clk);while(!wready);@(negedge ctrl_clk);wvalid=0;end
   join
   wait(bvalid);@(negedge ctrl_clk);check(bresp===0,"AXI-Lite writes accepted");
   repeat(2)@(negedge ctrl_clk);bready=1;@(negedge ctrl_clk);bready=0;
  end
 endtask
 task read_reg(input [7:0] addr,output [31:0] value);
  begin
   @(negedge ctrl_clk);araddr=addr;arvalid=1;do @(posedge ctrl_clk);while(!arready);@(negedge ctrl_clk);arvalid=0;
   wait(rvalid);@(negedge ctrl_clk);check(rresp===0,"AXI-Lite diagnostic read accepted");value=rdata;
   rready=1;@(negedge ctrl_clk);rready=0;
  end
 endtask
 task submit_case(input integer number);
  reg [31:0] value;begin
   read_reg(8,value);check(value[1]&&!value[0],"overlay channel ready before submission");
   for(integer j=0;j<21;j++)write_reg(commands[number*21+j][63:32],commands[number*21+j][31:0]);
   pending_case=number;commit_time=$realtime;
   $display("AV_SYSTEM_SUBMIT case=%0d time_ns=%0f",number,$realtime);
  end
 endtask
 // 按字节和行精确提供800×480 RGB565传感器数据；启动先发空帧同步
 // 以快速覆盖原厂丢弃前十帧的真实计数器。
 initial begin
  for(integer j=0;j<256;j++)forbidden[j]=0;
  wait(resetn);wait(!dut.cam_reset);repeat(20)@(negedge cam_clk);
  repeat(12)begin vsync=1;repeat(8)@(negedge cam_clk);vsync=0;repeat(8)@(negedge cam_clk);end
  vsync=1;repeat(200)@(negedge cam_clk);
  for(sensor_id=1;sensor_id<=6;sensor_id++)begin
   if(sensor_id==4)begin forbidden[4]=1;bad_reject_before=dut.video_reject;end
   vsync=0;repeat(10)@(negedge cam_clk);
   for(integer y=0;y<480;y++)begin
    href=1;
    for(integer x=0;x<800;x++)begin
     if(!(sensor_id==4&&x==799&&y==479))begin
      camera_byte=pattern(sensor_id,y*800+x)>>8;@(negedge cam_clk);
      camera_byte=pattern(sensor_id,y*800+x);@(negedge cam_clk);
     end
    end
    href=0;camera_byte=0;repeat(48)@(negedge cam_clk);
   end
   repeat(10)@(negedge cam_clk);vsync=1;repeat(200)@(negedge cam_clk);
   $display("AV_SYSTEM_SENSOR id=%0d sent=%0d complete=%0d reject=%0d time_ns=%0f",sensor_id,sensor_id==4?N-1:N,dut.video_complete,dut.video_reject,$realtime);
  end
  camera_done=1;href=0;vsync=1;camera_byte=0;
 end
 // AXI内存响应器各通道独立有界，延迟B/R并施加停顿。
 always @(posedge video_clk)begin
  if(!video_resetn)begin cyc<=0;wactive<=0;ractive<=0;mbvalid<=0;bdelay<=0;rdelay<=0;prev_aw=0;prev_ar=0;prev_w=0;end
  else begin
   cyc<=cyc+1;
   if(prev_aw)check(mawvalid&&mawaddr===saved_aw,"AW stable under backpressure");
   if(prev_ar)check(marvalid&&maraddr===saved_ar,"AR stable under backpressure");
   if(prev_w)check(mwvalid&&{mwlast,mstrb,mwdata}===saved_w,"W stable under backpressure");
   prev_aw=mawvalid&&!mawready;saved_aw=mawaddr;prev_ar=marvalid&&!marready;saved_ar=maraddr;
   prev_w=mwvalid&&!mwready;saved_w={mwlast,mstrb,mwdata};
   if(bdelay>0)begin bdelay<=bdelay-1;if(bdelay==1)mbvalid<=1;end
   if(rdelay>0)rdelay<=rdelay-1;
   if(mbvalid&&mbready)mbvalid<=0;
   if(mawvalid&&mawready)begin
    check(!wactive&&mawlen==15&&mawsize==3&&mawburst==1&&mawaddr[6:0]==0,"AW burst geometry");
    check(mawaddr>=BASE&&mawaddr<BASE+3*STRIDE&&((mawaddr-BASE)%STRIDE)<N*2,"AW reserved video payload");
    if(dut.frame_store.disp_valid)check((mawaddr-BASE)/STRIDE!=dut.frame_store.disp_bank,"write never overwrites DISPLAY bank");
    wbase=(mawaddr-BASE)/8;wb=0;wactive<=1;
   end
   if(mwvalid&&mwready)begin
    check(wactive&&mstrb==255&&mwlast==(wb==15),"W strobe and WLAST");
    memory[wbase+wb]<=mwdata;
    if(mwlast)begin wactive<=0;bdelay<=8;end
    wb++;
   end
   if(marvalid&&marready)begin
    check(!ractive&&marlen==15&&marsize==3&&marburst==1&&maraddr[6:0]==0,"AR burst geometry");
    check(maraddr>=BASE&&maraddr<BASE+3*STRIDE&&((maraddr-BASE)%STRIDE)<N*2,"AR reserved video payload");
    rbase<=(maraddr-BASE)/8;rb<=0;ractive<=1;rdelay<=5;
   end
   if(mrvalid&&mrready)begin if(mrlast)ractive<=0;rb<=rb+1;end
  end
 end
 // 端到端真值由输出帧号、传感器图案和C样本意图计算，
 // 不读取被测设计的像素、坐标、ID或字形逻辑。
 always @(negedge pix_clk)if(resetn&&!dut.disp_reset)begin
  pixel_tick++;
  if(vs&&!previous_vs)begin
   if(previous_vs_tick)check(pixel_tick-previous_vs_tick==1056*525,"full display VS period");
   previous_vs_tick=pixel_tick;
   if(index!=0)begin
    check(index==N&&rows==480,"800x480 complete raster");finished_frames++;
    if(frame_black)black_frames++;
    else begin
     check(image_id>=last_id,"displayed sensor IDs monotonic");
     if(image_id==last_id)repeated_ids++;last_id=image_id;full_frames++;
     if(active_case==0)valid_overlay_frames++;
     if(active_case==1)begin cleared_frames++;uncalibrated_frames++;end
     if(active_case==3)begin cleared_frames++;stale_frames++;end
     if(active_case==2)expiry_frames++;
    end
    $display("AV_SYSTEM_FRAME n=%0d sensor=%0d black=%0d overlay_case=%0d pixels=%0d time_ns=%0f",finished_frames,image_id,frame_black,active_case,index,$realtime);
    if(dump_frame)begin $fclose(fd);fd=0;dump_frame=0;saved_frames++;end
   end
   index=0;rows=0;row_pixels=0;frame_black=0;
   if(pending_case>=0)begin active_case=pending_case;active_commit_time=commit_time;pending_case=-1;end
   if(active_case==0&&saved_frames==0)begin fd=$fopen("av_system_frame.rgb565.hex","w");dump_frame=1;end
  end
  if(de)begin
   if(index==0)begin
    frame_black=(rgb===16'd0);image_id=(rgb>>8)&255;
    if(!frame_black)check(rgb[15:8]===rgb[7:0]&&image_id>=1&&image_id<=6&&!forbidden[image_id],"first pixel identifies a complete permitted sensor frame");
   end
   background=frame_black?0:pattern(image_id,index);expected=background;
   hit_kind=marker_at(index%800,index/800);
   if(!frame_black&&hit_kind>0&&(active_case==0||active_case==2))begin
    if(active_case==0||$realtime<active_commit_time+22000000.0)begin
     expected=(hit_kind==1||hit_kind==3)?{5'd4,6'd63,5'd12}:{5'd31,6'd47,5'd8};
     if(hit_kind==1)green_pixels++;else if(hit_kind==2)held_pixels++;else label_pixels++;
    end else if($realtime>active_commit_time+23002000.0)expired_points++;
    else begin ambiguous_marker_pixels++;expected=rgb;end
   end
   checks++;
   if(rgb!==expected)$fatal(1,"AV_SYSTEM_FAIL end-to-end RGB frame=%0d case=%0d x=%0d y=%0d actual=%h expected=%h time=%t",image_id,active_case,index%800,index/800,rgb,expected,$time);
   if(dump_frame)$fdisplay(fd,"%04x",rgb);
   index++;row_pixels++;checked_pixels++;
  end
  if(previous_de&&!de)begin check(row_pixels==800,"active line contains 800 pixels");row_pixels=0;rows++;end
  previous_de=de;previous_vs=vs;
 end
 reg [31:0] value,final_complete,final_reject,final_repeat,final_underflow,final_errors;
 initial begin
  smoke=$test$plusargs("SMOKE");
  $readmemh("av_system_commands.hex",commands);
  #1000;resetn=1;
  wait(!dut.control_reset);repeat(100)@(negedge ctrl_clk);
  read_reg(0,value);check(value==32'h4d415631,"current integrated video identity");
  if(smoke)begin
   #200000;check(dut.pll_locked&&video_resetn,"actual PLL model releases video reset");
   check(cyc>1000,"video AXI clock running");$display("AV_SYSTEM_SMOKE_PASS checks=%0d",checks);$finish;
  end
  wait(full_frames>=1);repeat(64)@(negedge pix_clk);submit_case(0);
  wait(valid_overlay_frames>=1);repeat(64)@(negedge pix_clk);submit_case(1);
  wait(cleared_frames>=1);repeat(64)@(negedge pix_clk);submit_case(2);
  wait(expiry_frames>=1);repeat(64)@(negedge pix_clk);submit_case(3);
  wait(camera_done&&repeated_ids>=2&&stale_frames>=1);repeat(100)@(negedge ctrl_clk);
  read_reg(8'h84,final_complete);read_reg(8'h88,final_reject);read_reg(8'h8c,final_repeat);
  read_reg(8'h94,final_underflow);read_reg(8'h98,final_errors);
  check(full_frames>=6&&black_frames>=1&&partial_frames==0,"normal full chain produces coherent frames with initial black wait");
  check(final_complete>=5&&final_reject>bad_reject_before,"malformed short sensor frame rejected");
  check(final_repeat>0&&repeated_ids>=2,"camera stop repeats last complete frame");
  check(final_underflow==0&&final_errors==0,"no unplanned underflow or AXI error");
  check(green_pixels>0&&held_pixels>0&&label_pixels>0&&expired_points>0,"current held ID and expiry pixels exercised");
  check(ambiguous_marker_pixels==0,"marker regions avoid global millisecond tick uncertainty band");
  check(saved_frames==1&&valid_overlay_frames>=1&&uncalibrated_frames>=1&&stale_frames>=1&&expiry_frames>=1,"all C fixture visibility phases complete");
  $display("AV_SYSTEM_FULL_PASS checks=%0d pixels=%0d full=%0d black=%0d partial=%0d complete=%0d reject=%0d repeat=%0d underflow=%0d errors=%0d green=%0d held=%0d labels=%0d expired=%0d ambiguous=%0d uncal=%0d stale=%0d",checks,checked_pixels,full_frames,black_frames,partial_frames,final_complete,final_reject,final_repeat,final_underflow,final_errors,green_pixels,held_pixels,label_pixels,expired_points,ambiguous_marker_pixels,uncalibrated_frames,stale_frames);
  $finish;
 end
 initial begin #220000000;$fatal(1,"AV system timeout full=%0d done=%0d valid=%0d clear=%0d expiry=%0d repeat=%0d",full_frames,camera_done,valid_overlay_frames,cleared_frames,expiry_frames,repeated_ids);end
endmodule
