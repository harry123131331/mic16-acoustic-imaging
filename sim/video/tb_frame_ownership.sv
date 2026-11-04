`timescale 1ns/1ps
module tb_frame_ownership;
`ifdef FULL_FRAME
 localparam integer N=384000;
 localparam [31:0] STRIDE=32'hc0000;
`else
 localparam integer N=512;
 localparam [31:0] STRIDE=4096;
`endif
 localparam [31:0] BASE=32'h10000000;
 reg clk=0,cam_clk=0,pix_clk=0,reset=1;
 always #5 clk=~clk;always #10 cam_clk=~cam_clk;always #15.277778 pix_clk=~pix_clk;
 reg camera_run=1,bad_next=0,stall_write=0,error_write_next=0,error_read_next=0,delay_read_next=0;
 reg cam_active=0,cam_valid=0;reg [15:0] cam_data=0;
 reg sof=0,request=0;
 wire [15:0] pixel;wire visible;wire [31:0] image_seq,completed,rejected,repeated,underflows,superseded,errors,ownership;
 wire [31:0] awaddr,araddr;wire [7:0] awlen,arlen,strb;wire [2:0] awsize,arsize;
 wire awvalid,wvalid,wlast,bready,arvalid,rready;wire [63:0] wdata;
 reg write_active=0,read_active=0,bvalid=0;reg [1:0] bresp=0,read_resp=0;
 integer cyc=0,wbase=0,rbase=0,wb=0,rb=0,bdelay=0,rdelay=0;
 reg [63:0] memory[0:3*STRIDE/8-1];
 wire awready=!stall_write&&!write_active&&!bvalid&&!bdelay&&(cyc%5!=0);
 wire wready=write_active&&(cyc%4!=0);
 wire arready=!read_active&&(cyc%7!=0);
 wire rvalid=read_active&&(rdelay==0);wire rlast=(rb==15);
 wire [63:0] rdata=memory[rbase+rb];
 integer checks=0,full_frames=0,black_frames=0,partial_frames=0,checked_pixels=0;
 integer gen_state=0,gen_count=0,gen_index=0,gen_id=0,gen_length=N;
 integer display_state=0,display_count=0,display_index=0,frame_visible=0;
 integer current_display_seq=0,last_full_seq=0;
 bit forbidden[0:255];reg black_seen=0;
 reg prev_aw=0,prev_ar=0,prev_w=0;reg [31:0] saved_aw,saved_ar;reg [72:0] saved_w;
 mic16_video_frame_store #(.FRAME_PIXELS(N),.BASE(BASE),.STRIDE(STRIDE)) dut(
 .clk(clk),.reset(reset),.cam_clk(cam_clk),.pix_clk(pix_clk),.cam_frame_active(cam_active),
 .cam_valid(cam_valid),.cam_data(cam_data),.frame_start(sof),.pixel_request(request),
 .pixel_data(pixel),.pixel_valid(visible),.pixel_frame_seq(image_seq),
 .completed_frames(completed),.rejected_frames(rejected),.repeated_frames(repeated),
 .underflow_frames(underflows),.superseded_frames(superseded),.axi_errors(errors),.ownership(ownership),
 .awaddr(awaddr),.awlen(awlen),.awsize(awsize),.awburst(),.awvalid(awvalid),.awready(awready),
 .wdata(wdata),.wstrb(strb),.wlast(wlast),.wvalid(wvalid),.wready(wready),
 .bresp(bresp),.bvalid(bvalid),.bready(bready),.araddr(araddr),.arlen(arlen),.arsize(arsize),
 .arburst(),.arvalid(arvalid),.arready(arready),.rdata(rdata),.rresp(read_resp),
 .rlast(rlast),.rvalid(rvalid),.rready(rready));
 function automatic [15:0] pattern(input integer id,input integer i);pattern=(id*257)^(i*73);endfunction
 task check(input logic ok,input string why);begin checks++;if(ok!==1'b1)
  $fatal(1,"OWNERSHIP_FAIL %s cyc=%0d gen=%0d display=%0d",why,cyc,gen_id,display_index);end endtask

 // 相机有效区早于首像素开始，并包含帧尾消隐。
 always @(negedge cam_clk)begin
  if(reset)begin cam_active=0;cam_valid=0;cam_data=0;gen_state=0;gen_count=0;gen_index=0;gen_id=0;end
  else case(gen_state)
   0:if(camera_run)begin
    gen_id++;cam_active=1;gen_length=bad_next?N-3:N;
    if(bad_next)begin forbidden[gen_id%256]=1;bad_next=0;end
    gen_count=3;gen_state=1;
   end
   1:if(gen_count>0)gen_count--;else begin gen_index=0;gen_state=2;end
   2:begin cam_valid=1;cam_data=pattern(gen_id,gen_index);gen_state=3;end
   3:begin cam_valid=0;gen_index++;if(gen_index==gen_length)begin gen_count=3;gen_state=4;end else gen_state=2;end
   4:if(gen_count>0)gen_count--;else begin cam_active=0;gen_count=140;gen_state=5;end
   5:if(gen_count>0)gen_count--;else gen_state=0;
  endcase
 end
 always @(posedge cam_clk)if(!reset&&dut.capturing&&dut.cap_pixel_bad)
  forbidden[gen_id%256]=1;

 // 显示帧在足够垂直消隐后连续请求512次。
 always @(negedge pix_clk)begin
  if(reset)begin sof=0;request=0;display_state=0;display_count=0;end
  else case(display_state)
   0:begin sof=1;request=0;display_count=800;display_state=1;end
   1:begin sof=0;if(display_count>0)display_count--;else begin request=1;display_count=N;display_state=2;end end
   2:if(display_count>1)display_count--;else begin request=0;display_state=0;end
  endcase
 end
 always @(posedge pix_clk)begin
  if(reset)begin display_index=0;frame_visible=0;black_seen=0;last_full_seq=0;end
  else if(sof)begin
   if(display_index!=0)begin
    check(display_index==N,"exact display request count");
    if(frame_visible==N)begin check(current_display_seq>=last_full_seq,"frame IDs never go backward");last_full_seq=current_display_seq;full_frames++;end
    else if(frame_visible==0)black_frames++;else partial_frames++;
   end
   display_index=0;frame_visible=0;black_seen=0;
  end else if(request)begin
   if(visible)begin
    check(!black_seen,"late-ready must not resume pixels in middle of frame");
    if(display_index==0)current_display_seq=dut.pixel_ready_image;
    check(dut.pixel_ready_image==current_display_seq,"one frame ID for entire display period");
    check(!forbidden[current_display_seq%256],"malformed/overflow/error write frame never displayed");
    check(pixel===pattern(current_display_seq,display_index),"every pixel matches independent camera frame/index");
    frame_visible++;checked_pixels++;
   end else begin check(pixel===16'd0,"invalid output black");black_seen=1;end
   display_index++;
  end
 end
 always @(posedge clk)begin
  if(reset)begin cyc<=0;write_active<=0;read_active<=0;bvalid<=0;bdelay<=0;rdelay<=0;prev_aw=0;prev_ar=0;prev_w=0;end
  else begin
   cyc<=cyc+1;
   if(cyc<12000&&dut.finish_frame)$display("FINISH bank=%0d good=%b seq=%0d offset=%0d mbad=%b wbad=%b",dut.write_bank,dut.finish_good,dut.meta_seq,dut.write_offset,dut.meta_bad,dut.write_bad);
   if(cyc<12000&&dut.switch_frame)$display("SWITCH valid=%b bank=%0d seq=%0d",dut.next_valid,dut.next_bank,dut.next_seq);
   if(prev_aw)check(awvalid&&awaddr===saved_aw,"AW stable during stalls");
   if(prev_ar)check(arvalid&&araddr===saved_ar,"AR stable during stalls");
   if(prev_w)check(wvalid&&{wlast,strb,wdata}===saved_w,"W stable during stalls");
   prev_aw=awvalid&&!awready;saved_aw=awaddr;prev_ar=arvalid&&!arready;saved_ar=araddr;
   prev_w=wvalid&&!wready;saved_w={wlast,strb,wdata};
   if(bdelay>0)begin bdelay<=bdelay-1;if(bdelay==1)bvalid<=1;end
   if(rdelay>0)rdelay<=rdelay-1;
   if(bvalid&&bready)bvalid<=0;
   if(awvalid&&awready)begin
    check(!write_active&&awlen==15&&awsize==3&&awaddr[6:0]==0,"AW geometry");
    check(awaddr>=BASE&&awaddr<BASE+3*STRIDE,"AW bank range");
    check(((awaddr-BASE)%STRIDE)<N*2,"AW inside image payload");
    if(dut.disp_valid)check((awaddr-BASE)/STRIDE!=dut.disp_bank,"never overwrite displayed bank");
    wbase=(awaddr-BASE)/8;wb=0;write_active<=1;
   end
   if(wvalid&&wready)begin
    check(write_active&&strb==255&&wlast==(wb==15),"W beat/strobe/last");
    if(dut.disp_valid)check(((wbase+wb)*8)/STRIDE!=dut.disp_bank,"display bank remains protected until final B");
    memory[wbase+wb]<=wdata;
    if(wlast)begin
     write_active<=0;bdelay<=8;
     if(error_write_next)begin bresp<=2;error_write_next=0;forbidden[memory[(wbase/(STRIDE/8))*(STRIDE/8)][63:56]]=1;end else bresp<=0;
    end
    wb++;
   end
   if(arvalid&&arready)begin
    check(!read_active&&arlen==15&&arsize==3&&araddr[6:0]==0,"AR geometry");
    check(araddr>=BASE&&araddr<BASE+3*STRIDE,"AR bank range");
    check(((araddr-BASE)%STRIDE)<N*2,"AR inside image payload");
    check(dut.disp_valid,"no read before first complete frame");
    rbase<=(araddr-BASE)/8;rb<=0;read_active<=1;
    if(error_read_next)begin read_resp<=2;error_read_next=0;end else read_resp<=0;
    if(delay_read_next)begin rdelay<=10000;delay_read_next=0;end else rdelay<=5;
   end
   if(rvalid&&rready)begin if(rlast)read_active<=0;rb<=rb+1;end
   if(dut.disp_valid)check(!dut.free_mask[dut.disp_bank],"display ownership is not FREE");
   if(dut.ready_valid)begin check(!dut.free_mask[dut.ready_bank],"ready ownership is not FREE");
    if(dut.disp_valid)check(dut.ready_bank!=dut.disp_bank,"READY and DISPLAY distinct");end
  end
 end
 integer old_repeats,old_rejected,old_errors,old_full;
 initial begin
  for(integer i=0;i<256;i++)forbidden[i]=0;
  repeat(30)@(negedge clk);reset=0;
`ifdef FULL_FRAME
  #90000000;
  check(completed>=3&&full_frames>=3,"full 800x480 concurrent frames coherent");
  camera_run=0;#20000000;old_repeats=repeated;old_full=full_frames;#30000000;
  check(repeated>old_repeats&&full_frames>old_full,"full resolution stopped camera repeats");
  $display("FRAME_OWNERSHIP_FULL_PASS checks=%0d pixels=%0d full_frames=%0d black=%0d partial=%0d completed=%0d repeated=%0d",checks,checked_pixels,full_frames,black_frames,partial_frames,completed,repeated);
  $finish;
`else
  #400000;
  $display("NORMAL_DIAG completed=%0d rejected=%0d full=%0d pixels=%0d ws=%0d rs=%0d meta=%b/%b cap=%0d grant=%b/%b banks=%b disp=%b/%0d ack=%b/%b seq=%0d/%0d count=%0d",completed,rejected,full_frames,checked_pixels,dut.ws,dut.rs,dut.meta_seen,dut.meta_bad,dut.cap_count,dut.grant_held,dut.capturing,dut.free_mask,dut.disp_valid,dut.disp_bank,dut.ack_pending,dut.ack_sent,dut.pixel_ready_req,dut.pixel_request_seq,dut.rf_pixels);
  check(completed>=5&&full_frames>=4,"ordinary concurrent camera/display works");
  check(superseded>0,"fast camera replaces only pending READY frames");
  $display("OWNERSHIP_PHASE normal full=%0d complete=%0d superseded=%0d",full_frames,completed,superseded);
  camera_run=0;#40000;old_repeats=repeated;#120000;
  check(repeated>old_repeats,"stopped camera repeats last complete image");camera_run=1;
  old_rejected=rejected;bad_next=1;#100000;check(rejected>old_rejected,"short camera frame rejected");
  old_rejected=rejected;stall_write=1;#80000;stall_write=0;#150000;
  check(rejected>old_rejected,"capture overflow rejects complete frame");
  old_errors=errors;old_rejected=rejected;error_write_next=1;#100000;
  check(errors>old_errors&&rejected>old_rejected,"BRESP error bank not published");
  old_errors=errors;error_read_next=1;#120000;check(errors>old_errors,"RRESP error detected and next frame recovers");
  wait(visible);delay_read_next=1;#220000;check(underflows>0,"long read delay reports underflow and blanks tail");
  old_full=full_frames;#120000;check(full_frames>old_full,"recovery after fault produces complete images");
  $display("OWNERSHIP_PHASE faults rejected=%0d axi_errors=%0d underflow=%0d partial=%0d",rejected,errors,underflows,partial_frames);
  @(negedge clk);reset=1;repeat(30)@(negedge clk);for(integer i=0;i<256;i++)forbidden[i]=0;reset=0;
  old_full=full_frames;#220000;check(completed>=2&&full_frames>old_full,"reset hides stale memory then recovers");
  $display("FRAME_OWNERSHIP_PASS checks=%0d pixels=%0d full_frames=%0d black=%0d partial=%0d completed=%0d rejected=%0d repeated=%0d superseded=%0d errors=%0d underflows=%0d",checks,checked_pixels,full_frames,black_frames,partial_frames,completed,rejected,repeated,superseded,errors,underflows);
  $finish;
`endif
 end
`ifdef FULL_FRAME
 initial begin #180000000;$fatal(1,"full ownership timeout");end
`else
 initial begin #3000000;$fatal(1,"ownership timeout");end
`endif
endmodule
