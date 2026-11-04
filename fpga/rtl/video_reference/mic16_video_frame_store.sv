`timescale 1ns/1ps
// 保存三幅完整DDR帧，复用原配置的16/64及64/16 FIFO。
// 共同复位且所有时钟运行；多位描述符通过队列跨时钟域。
// 固定64位、16拍INCR突发，FRAME_PIXELS必须能被64整除。
module mic16_video_frame_store #(
 parameter integer FRAME_PIXELS=384000,
 parameter [31:0] BASE=32'h10000000,parameter [31:0] STRIDE=32'h000c0000,
 parameter integer RESET_CYCLES=64,parameter integer PREFILL_PIXELS=32,parameter integer ENABLE_EXPORT=0
)(
 input wire clk,input wire reset,input wire cam_clk,input wire pix_clk,
 input wire cam_frame_active,input wire cam_valid,input wire [15:0] cam_data,
 input wire frame_start,input wire pixel_request,
 output wire [15:0] pixel_data,output wire pixel_valid,output reg [31:0] pixel_frame_seq,
 output reg [31:0] completed_frames,output reg [31:0] rejected_frames,
 output reg [31:0] repeated_frames,output reg [31:0] underflow_frames,
 output wire [31:0] superseded_frames,output reg [31:0] axi_errors,
 output wire [31:0] ownership,output wire [31:0] display_sequence,
 input wire acquire_valid,output wire acquire_ready,output wire export_valid,output wire [1:0] export_bank,
 output wire [31:0] export_seq,output wire [31:0] export_token,
 input wire release_valid,input wire [31:0] release_token,output wire release_accepted,output wire token_exhausted,
 output wire [31:0] awaddr,output wire [7:0] awlen,output wire [2:0] awsize,
 output wire [1:0] awburst,output wire awvalid,input wire awready,
 output wire [63:0] wdata,output wire [7:0] wstrb,output wire wlast,
 output wire wvalid,input wire wready,input wire [1:0] bresp,input wire bvalid,output wire bready,
 output wire [31:0] araddr,output wire [7:0] arlen,output wire [2:0] arsize,
 output wire [1:0] arburst,output wire arvalid,input wire arready,
 input wire [63:0] rdata,input wire [1:0] rresp,input wire rlast,input wire rvalid,output wire rready
);
 wire cam_reset,pix_reset;
 xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) camera_reset_sync(
 .src_arst(reset),.dest_clk(cam_clk),.dest_arst(cam_reset));
 xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) pixel_reset_sync(
 .src_arst(reset),.dest_clk(pix_clk),.dest_arst(pix_reset));
 localparam [31:0] FRAME_BYTES=FRAME_PIXELS*2;
 localparam [2:0] WI=0,WS=1,WA=2,WD=3,WB=4,WE=5,WC=6;
 localparam [1:0] RI=0,RA=1,RD=2,RW=3;
 reg [2:0] ws;reg [1:0] rs;
 reg [1:0] write_bank;reg [31:0] write_offset;
 reg [4:0] fill;reg [3:0] write_beat;reg [7:0] read_beat;
 reg [63:0] burst_buffer[0:15];
 reg meta_seen,meta_bad,write_bad;reg [31:0] meta_seq;
 reg wr_reset;reg [15:0] wr_hold;
 reg rd_reset,read_fence,read_bad,ack_pending,ack_sent;reg [15:0] rd_hold;
 reg [31:0] read_offset,request_seq,ack_request,ack_image;reg ack_good;
 wire wf_empty,wf_full,wf_wbusy,wf_rbusy,rf_empty,rf_full,rf_wbusy,rf_rbusy;
 wire [63:0] wf_data;wire [15:0] rf_data;
 wire [5:0] wf_count,rf_count;wire [7:0] rf_pixels,wf_pixels;
 wire wf_pop,rf_push,rf_pop;wire [15:0] source_pixel;
 wire source_write;
 (* ASYNC_REG="TRUE" *) reg [1:0] wf_wbusy_sync,rf_rbusy_sync;
 always @(posedge clk)begin
  if(reset)begin wf_wbusy_sync<=3;rf_rbusy_sync<=3;end
  else begin wf_wbusy_sync<={wf_wbusy_sync[0],wf_wbusy};rf_rbusy_sync<={rf_rbusy_sync[0],rf_rbusy};end
 end
 wire write_fifo_busy=wf_rbusy||wf_wbusy_sync[1];
 wire read_fifo_busy=rf_wbusy||rf_rbusy_sync[1];

 // 一次只授权一个相机帧；溢出时帧结束描述符不能丢失。
 wire grant_ready,grant_valid,grant_pop;wire [1:0] grant_data;
 wire bank_available;wire [1:0] allocated_bank;
 wire allocate=(ws==WI)&&bank_available&&grant_ready&&!write_fifo_busy&&!wr_reset;
 mic16_video_queue #(.WIDTH(2)) grants(reset,clk,cam_clk,allocated_bank,allocate,grant_ready,grant_data,grant_valid,grant_pop);
 reg grant_held,capturing,cam_previous,cap_bad,report_pending;
 reg [1:0] held_bank,cap_bank;reg [20:0] cap_count;reg [31:0] cam_seq,cap_seq;
 reg [63:0] report_data;
 wire report_ready,report_valid,report_pop;wire [63:0] report_received;
 mic16_video_queue #(.WIDTH(64)) reports(reset,cam_clk,clk,report_data,report_pending,report_ready,report_received,report_valid,report_pop);
 assign grant_pop=!grant_held&&!capturing&&!report_pending;
 assign source_pixel=cam_data;
 assign source_write=capturing&&cam_valid&&!cap_bad&&(cap_count<FRAME_PIXELS)&&!wf_full&&!wf_wbusy;
 wire cap_pixel_bad=cam_valid&&(wf_full||wf_wbusy||(cap_count>=FRAME_PIXELS));
 wire [20:0] final_count=cap_count+(cam_valid?1:0);
 always @(posedge cam_clk)begin
  if(cam_reset)begin grant_held<=0;capturing<=0;cam_previous<=0;cap_bad<=0;
   cap_count<=0;cam_seq<=0;cap_seq<=0;held_bank<=0;cap_bank<=0;report_pending<=0;report_data<=0;end
  else begin
   cam_previous<=cam_frame_active;
   if(report_pending&&report_ready)report_pending<=0;
   if(grant_valid&&grant_pop)begin held_bank<=grant_data;grant_held<=1;end
   if(cam_frame_active&&!cam_previous)begin
    cam_seq<=cam_seq+1;
    if(grant_held&&!wf_wbusy)begin
     capturing<=1;cap_bank<=held_bank;grant_held<=0;cap_count<=0;cap_bad<=0;cap_seq<=cam_seq+1;
    end
   end
   if(capturing&&cam_valid)begin cap_count<=cap_count+1;if(cap_pixel_bad)cap_bad<=1;end
   if(capturing&&!cam_frame_active&&cam_previous)begin
    capturing<=0;report_pending<=1;
    report_data<={8'd0,final_count,(cap_bad||cap_pixel_bad||(final_count!=FRAME_PIXELS)),cap_bank,cap_seq};
   end
  end
 end

 // 显示帧请求与对应就绪描述符都携带完整请求ID。
 reg [31:0] pixel_request_seq;wire req_ready,req_valid;wire [31:0] req_received;
 wire command=req_valid;
 mic16_video_queue #(.WIDTH(32)) requests(reset,pix_clk,clk,pixel_request_seq+1,frame_start,req_ready,req_received,req_valid,1'b1);
 wire ack_ready,ack_valid;wire [95:0] ack_received;
 mic16_video_queue #(.WIDTH(96)) acks(reset,clk,pix_clk,{31'd0,ack_good,ack_image,ack_request},ack_pending,ack_ready,ack_received,ack_valid,1'b1);
 reg [31:0] pixel_ready_req,pixel_ready_image;
 reg pixel_ready_good,frame_started,show_frame,queue_fault,pixel_underflow;
 wire can_show=pixel_ready_good&&(pixel_ready_req==pixel_request_seq)&&!queue_fault;
 wire start_ok=can_show&&(rf_pixels>=PREFILL_PIXELS)&&!rf_empty&&!rf_rbusy;
 wire allow_pixel=frame_started?(show_frame&&can_show):start_ok;
 assign pixel_valid=pixel_request&&allow_pixel&&!rf_empty&&!rf_rbusy&&!frame_start;
 assign rf_pop=pixel_valid;
 assign pixel_data=pixel_valid?rf_data:16'd0;
 always @(posedge pix_clk)begin
  if(pix_reset)begin pixel_request_seq<=0;pixel_ready_req<=0;pixel_ready_image<=0;
   pixel_ready_good<=0;frame_started<=0;show_frame<=0;queue_fault<=0;pixel_underflow<=0;pixel_frame_seq<=0;end
  else begin
   pixel_underflow<=0;
   if(ack_valid)begin pixel_ready_req<=ack_received[31:0];pixel_ready_image<=ack_received[63:32];pixel_ready_good<=ack_received[64];end
   if(frame_start)begin
    pixel_request_seq<=pixel_request_seq+1;frame_started<=0;show_frame<=0;
    // FIFO启动或满时跳过本次请求，不锁存永久故障。
    // 下一次接受的帧获得新ID；迟到确认不能使帧中途恢复显示。
    if(req_ready)queue_fault<=0;else queue_fault<=1;
   end else if(pixel_request)begin
    if(!frame_started)begin frame_started<=1;show_frame<=start_ok;pixel_frame_seq<=pixel_ready_image;end
    else if(show_frame&&(!can_show||rf_empty||rf_rbusy))begin show_frame<=0;pixel_underflow<=1;end
   end
  end
 end
 wire underflow_event;
 xpm_cdc_pulse #(.DEST_SYNC_FF(2),.REG_OUTPUT(1),.RST_USED(1),.SIM_ASSERT_CHK(0)) underflow_cdc(
 .src_clk(pix_clk),.src_rst(pix_reset),.src_pulse(pixel_underflow),.dest_clk(clk),.dest_rst(reset),.dest_pulse(underflow_event));

 wire disp_valid,ready_valid,next_valid;wire [1:0] disp_bank,ready_bank,next_bank;
 wire [31:0] disp_seq,ready_seq,next_seq;wire [2:0] free_mask;
 wire reader_idle=(rs==RI)||(rs==RW);
 wire switch_frame=read_fence&&reader_idle&&!rd_reset&&!read_fifo_busy&&!command;
 wire finish_frame=(ws==WE)&&meta_seen;
 wire finish_good=!write_bad&&!meta_bad&&(write_offset==FRAME_BYTES);
 mic16_frame_banks_export #(.ENABLE_EXPORT(ENABLE_EXPORT)) banks(.clk(clk),.reset(reset),.allocate(allocate),.available(bank_available),.allocation(allocated_bank),
 .finish(finish_frame),.finish_good(finish_good),.finish_bank(write_bank),.finish_seq(meta_seq),
 .switch_display(switch_frame),.next_display_valid(next_valid),.next_display_bank(next_bank),.next_display_seq(next_seq),
 .display_valid(disp_valid),.display_bank(disp_bank),.display_seq(disp_seq),
 .ready_valid(ready_valid),.ready_bank(ready_bank),.ready_seq(ready_seq),.free_mask(free_mask),.superseded(superseded_frames),
 .acquire_valid(acquire_valid),.acquire_ready(acquire_ready),.export_valid(export_valid),
 .export_bank(export_bank),.export_seq(export_seq),.export_token(export_token),
 .release_valid(release_valid),.release_token(release_token),.release_accepted(release_accepted),.token_exhausted(token_exhausted));
 assign ownership={16'd0,3'd0,write_bank,ready_bank,disp_bank,ready_valid,disp_valid,free_mask,(ws!=WI)};
 assign display_sequence=disp_seq;

 wr_ddr3_fifo write_pixels(.rst(reset||wr_reset),.wr_clk(cam_clk),.rd_clk(clk),.din(source_pixel),
 .wr_en(source_write),.rd_en(wf_pop),.dout(wf_data),.full(wf_full),.empty(wf_empty),
 .wr_data_count(wf_pixels),.rd_data_count(wf_count),.wr_rst_busy(wf_wbusy),.rd_rst_busy(wf_rbusy));
 rd_ddr3_fifo read_pixels(.rst(reset||rd_reset||frame_start),.wr_clk(clk),.rd_clk(pix_clk),.din(rdata),
 .wr_en(rf_push),.rd_en(rf_pop),.dout(rf_data),.full(rf_full),.empty(rf_empty),
 .wr_data_count(rf_count),.rd_data_count(rf_pixels),.wr_rst_busy(rf_wbusy),.rd_rst_busy(rf_rbusy));

 assign wf_pop=(ws==WS)&&(fill<16)&&!wf_empty&&!write_fifo_busy&&!(meta_seen&&meta_bad)&&!write_bad;
 assign report_pop=(ws!=WI)&&(ws!=WC)&&!meta_seen;
 assign awaddr=BASE+write_bank*STRIDE+write_offset;
 assign awlen=15;assign awsize=3;assign awburst=1;assign awvalid=(ws==WA);
 assign wdata=burst_buffer[write_beat];assign wstrb=8'hff;
 assign wvalid=(ws==WD);assign wlast=(write_beat==15);assign bready=(ws==WB);
 assign araddr=BASE+disp_bank*STRIDE+read_offset;
 assign arlen=15;assign arsize=3;assign arburst=1;assign arvalid=(rs==RA);
 wire discard_read=read_fence||command||rd_reset||read_bad;
 assign rready=(rs==RD)&&(discard_read||(!rf_full&&!read_fifo_busy));
 assign rf_push=(rs==RD)&&rvalid&&rready&&!discard_read;
 wire write_error=(ws==WB)&&bvalid&&(bresp!=0);
 wire read_error=(rs==RD)&&rvalid&&rready&&((rresp!=0)||(rlast!=(read_beat==15)));

 always @(posedge clk)begin
  if(reset)begin
   ws<=WC;write_bank<=0;write_offset<=0;fill<=0;write_beat<=0;wr_reset<=1;wr_hold<=RESET_CYCLES;
   meta_seen<=0;meta_bad<=0;meta_seq<=0;write_bad<=0;
   rs<=RI;read_offset<=0;read_beat<=0;rd_reset<=1;rd_hold<=RESET_CYCLES;read_fence<=0;read_bad<=0;
   request_seq<=0;ack_pending<=0;ack_sent<=0;ack_request<=0;ack_image<=0;ack_good<=0;
   completed_frames<=0;rejected_frames<=0;repeated_frames<=0;underflow_frames<=0;axi_errors<=0;
  end else begin
   if(underflow_event)underflow_frames<=underflow_frames+1;
   if(write_error||read_error)axi_errors<=axi_errors+(write_error?1:0)+(read_error?1:0);
   if(report_valid&&report_pop)begin
    meta_seen<=1;meta_seq<=report_received[31:0];
    meta_bad<=report_received[34]||(report_received[33:32]!=write_bank)||(report_received[55:35]!=FRAME_PIXELS);
   end
   case(ws)
    WI:if(allocate)begin write_bank<=allocated_bank;write_offset<=0;fill<=0;meta_seen<=0;meta_bad<=0;write_bad<=0;ws<=WS;end
    WS:begin
     if(write_bad||(meta_seen&&meta_bad))ws<=WE;
     else if(fill==16)begin write_beat<=0;ws<=WA;end
     else if(wf_pop)begin burst_buffer[fill[3:0]]<=wf_data;fill<=fill+1;end
    end
    WA:if(awready)begin write_beat<=0;ws<=WD;end
    WD:if(wready)begin if(wlast)ws<=WB;else write_beat<=write_beat+1;end
    WB:if(bvalid)begin
     if(bresp!=0)begin write_bad<=1;ws<=WE;end
     else begin write_offset<=write_offset+128;fill<=0;if(write_offset+128>=FRAME_BYTES)ws<=WE;else ws<=WS;end
    end
    WE:if(meta_seen)begin
     if(finish_good)completed_frames<=completed_frames+1;else rejected_frames<=rejected_frames+1;
     wr_reset<=1;wr_hold<=RESET_CYCLES;fill<=0;ws<=WC;
    end
    WC:if(wr_hold!=0)wr_hold<=wr_hold-1;
       else if(wr_reset)wr_reset<=0;
       else if(!write_fifo_busy)ws<=WI;
   endcase

   if(ack_pending&&ack_ready)begin ack_pending<=0;ack_sent<=1;end
   if(rd_hold!=0)rd_hold<=rd_hold-1;
   else if(rd_reset&&reader_idle)rd_reset<=0;
   case(rs)
    RA:if(arready)begin rs<=RD;read_beat<=0;end
    RD:if(rvalid&&rready)begin
     read_beat<=read_beat+1;
     if(read_error)begin
      read_bad<=1;
      if(!read_bad&&!read_fence&&!command)begin ack_good<=0;ack_request<=request_seq;ack_image<=disp_seq;ack_pending<=1;end
     end
     if(rlast)begin read_offset<=read_offset+128;rs<=RW;end
    end
    RW:if(!read_fence&&!command&&!read_bad&&!rd_reset&&!read_fifo_busy&&(read_offset<FRAME_BYTES)&&(rf_count<=15))rs<=RA;
   endcase
   if(switch_frame)begin
    read_fence<=0;read_bad<=0;read_offset<=0;rs<=next_valid?RA:RI;
    ack_request<=request_seq;ack_image<=next_seq;ack_good<=next_valid;ack_pending<=!next_valid;
    if(next_valid&&!ready_valid)repeated_frames<=repeated_frames+1;
   end
   // 至少预取完整一个突发后才发布匹配元数据。
   if(!read_fence&&!command&&ack_good&&!ack_pending&&!ack_sent&&(read_offset>=128))begin
    ack_pending<=1;
   end
   if(command)begin
    request_seq<=req_received;read_fence<=1;rd_reset<=1;rd_hold<=RESET_CYCLES;ack_pending<=0;ack_sent<=0;ack_good<=0;
   end
  end
 end
endmodule
