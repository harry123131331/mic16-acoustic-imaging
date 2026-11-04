`timescale 1ns/1ps
// Derived from the imported vendor ch41 top; original source is preserved.
// Three complete DDR banks; ownership changes only at drained display boundaries.
module mic16_camera_path(
 input wire reset_n,input wire ps2pl_clk50m_0,input wire ps2pl_resetn_0,
 (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 ctrl_clk CLK", X_INTERFACE_PARAMETER="ASSOCIATED_BUSIF S_AXI, ASSOCIATED_RESET ctrl_resetn" *) input wire ctrl_clk,
 (* X_INTERFACE_INFO="xilinx.com:signal:reset:1.0 ctrl_resetn RST", X_INTERFACE_PARAMETER="POLARITY ACTIVE_LOW" *) input wire ctrl_resetn,
 output wire led,output wire camera_sclk,inout wire camera_sdat,
 input wire camera_vsync,input wire camera_href,input wire camera_pclk,
 output wire camera_xclk,input wire [7:0] camera_data,
 output wire [15:0] TFT_rgb,output wire TFT_hs,output wire TFT_vs,
 output wire TFT_clk,output wire TFT_de,output wire TFT_pwm,
 output wire SiI9022_sclk,inout wire SiI9022_sdat,
 (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 m_video_aclk CLK", X_INTERFACE_PARAMETER="ASSOCIATED_BUSIF M_VIDEO, ASSOCIATED_RESET m_video_resetn" *) output wire m_video_aclk,
 (* X_INTERFACE_INFO="xilinx.com:signal:reset:1.0 m_video_resetn RST", X_INTERFACE_PARAMETER="POLARITY ACTIVE_LOW" *) output wire m_video_resetn,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWADDR", X_INTERFACE_PARAMETER="XIL_INTERFACENAME M_VIDEO, PROTOCOL AXI4, DATA_WIDTH 64, ADDR_WIDTH 32, ID_WIDTH 0, MAX_BURST_LENGTH 16" *) output wire [31:0] m_video_awaddr,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWLEN" *) output wire [7:0] m_video_awlen,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWSIZE" *) output wire [2:0] m_video_awsize,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWBURST" *) output wire [1:0] m_video_awburst,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWLOCK" *) output wire m_video_awlock,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWCACHE" *) output wire [3:0] m_video_awcache,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWPROT" *) output wire [2:0] m_video_awprot,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWQOS" *) output wire [3:0] m_video_awqos,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWREGION" *) output wire [3:0] m_video_awregion,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWVALID" *) output wire m_video_awvalid,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO AWREADY" *) input wire m_video_awready,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO WDATA" *) output wire [63:0] m_video_wdata,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO WSTRB" *) output wire [7:0] m_video_wstrb,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO WLAST" *) output wire m_video_wlast,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO WVALID" *) output wire m_video_wvalid,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO WREADY" *) input wire m_video_wready,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO BRESP" *) input wire [1:0] m_video_bresp,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO BVALID" *) input wire m_video_bvalid,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO BREADY" *) output wire m_video_bready,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARADDR" *) output wire [31:0] m_video_araddr,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARLEN" *) output wire [7:0] m_video_arlen,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARSIZE" *) output wire [2:0] m_video_arsize,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARBURST" *) output wire [1:0] m_video_arburst,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARLOCK" *) output wire m_video_arlock,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARCACHE" *) output wire [3:0] m_video_arcache,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARPROT" *) output wire [2:0] m_video_arprot,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARQOS" *) output wire [3:0] m_video_arqos,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARREGION" *) output wire [3:0] m_video_arregion,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARVALID" *) output wire m_video_arvalid,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO ARREADY" *) input wire m_video_arready,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO RDATA" *) input wire [63:0] m_video_rdata,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO RRESP" *) input wire [1:0] m_video_rresp,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO RLAST" *) input wire m_video_rlast,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO RVALID" *) input wire m_video_rvalid,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 M_VIDEO RREADY" *) output wire m_video_rready,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWADDR", X_INTERFACE_PARAMETER="XIL_INTERFACENAME S_AXI, PROTOCOL AXI4LITE, DATA_WIDTH 32, ADDR_WIDTH 8" *) input wire [7:0] s_axi_awaddr,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWPROT" *) input wire [2:0] s_axi_awprot,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *) input wire s_axi_awvalid,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *) output wire s_axi_awready,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WDATA" *) input wire [31:0] s_axi_wdata,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WSTRB" *) input wire [3:0] s_axi_wstrb,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WVALID" *) input wire s_axi_wvalid,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WREADY" *) output wire s_axi_wready,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI BRESP" *) output wire [1:0] s_axi_bresp,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI BVALID" *) output wire s_axi_bvalid,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI BREADY" *) input wire s_axi_bready,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARADDR" *) input wire [7:0] s_axi_araddr,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARPROT" *) input wire [2:0] s_axi_arprot,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *) input wire s_axi_arvalid,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *) output wire s_axi_arready,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RDATA" *) output wire [31:0] s_axi_rdata,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RRESP" *) output wire [1:0] s_axi_rresp,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RVALID" *) output wire s_axi_rvalid,
 (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RREADY" *) input wire s_axi_rready
);
//Resolution_800x480  像素时钟为33MHz
parameter IMAGE_WIDTH  = 800;
parameter IMAGE_HEIGHT = 480;

//PL使用DDR的基地址，留一定空间给PS用
parameter DDR_BASE_ADDR = 32'h10000000;

//*********************************
//Internal connect
//*********************************
  //clock
  wire          pll_locked;
  wire          loc_clk50m;
  wire          loc_clk24m;
  wire          loc_clk100m;
  wire          disp_clk;
  wire          disp_clk5x;
  wire          reset;
  //camera interface
  wire          camera_init_done;
  wire hdmi_init_done;
  wire [1:0] config_ready_video;
  wire [31:0] config_status={16'h0008,14'd0,config_ready_video};
  xpm_cdc_array_single #(.DEST_SYNC_FF(2),.INIT_SYNC_FF(1),.SIM_ASSERT_CHK(0),.SRC_INPUT_REG(0),.WIDTH(2)) config_ready_sync(
    .src_clk(loc_clk50m),.src_in({hdmi_init_done,camera_init_done}),.dest_clk(loc_clk100m),.dest_out(config_ready_video));
  wire          pclk_bufg_o;
  wire [15:0]   image_data;
  wire          image_data_valid;
  wire          image_data_hs;
  wire          image_data_vs;
  //tft
  wire          frame_begin;
  wire          disp_data_req;
  wire [15:0]   disp_data;
  //VGA
  wire [4:0]Disp_Red;
  wire [5:0]Disp_Green;
  wire [4:0]Disp_Blue;
  wire [3:0] m_video_bid,m_video_rid;
  wire raw_de,raw_hs,raw_vs;
  assign m_video_bid=4'd0;
  assign m_video_rid=4'd0;
  //wr_fifo Interface
  wire          wrfifo_clr;
  wire [15:0]   wrfifo_din;
  wire          wrfifo_wren;
  //rd_fifo Interface
  wire          rdfifo_clr;
  wire          rdfifo_rden;
  wire [15 :0]  rdfifo_dout;
  //axi
  wire [3:0]    m_video_awid;
  //
  //
  //
  wire [3:0]    m_video_arid;
  //
  //
  //
  wire [7:0]    uart_byte;
  wire          uart_byte_vaild;

  assign loc_clk50m        = ps2pl_clk50m_0;
  assign m_video_aclk        = loc_clk100m;
  assign led = {camera_init_done};
  
  wire          pl_reset_n;
  wire          reset_pre;
  reg  [19:0]   reset_sync;
  assign pl_reset_n = ps2pl_resetn_0 & reset_n;
  assign reset_pre     = ~pll_locked;

  //PS先释放复位，PL的逻辑复位释放往后延迟20个时钟周期
  always@(posedge loc_clk100m or posedge reset_pre)
  begin
    if(reset_pre)
      reset_sync <= {20{1'b1}};
    else
      reset_sync <= reset_sync << 1;
  end

  assign reset = reset_sync[19];
  assign m_video_resetn = !reset;
  // Each external/video domain releases reset on its own running clock.
  wire cam_reset,disp_reset,ref_reset,control_reset;
  xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) cam_reset_sync(
    .src_arst(reset),.dest_clk(pclk_bufg_o),.dest_arst(cam_reset));
  xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) disp_reset_sync(
    .src_arst(reset),.dest_clk(disp_clk),.dest_arst(disp_reset));
  xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) ref_reset_sync(
    .src_arst(reset),.dest_clk(loc_clk50m),.dest_arst(ref_reset));
  xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) control_reset_sync(
    .src_arst(reset||!ctrl_resetn),.dest_clk(ctrl_clk),.dest_arst(control_reset));


  pll pll
  (
    // Clock out ports
    .clk_out1 (loc_clk100m     ), // output clk_out1
    .clk_out2 (disp_clk        ), // output clk_out2
    .clk_out3 (loc_clk24m      ), // output clk_out3
    // Status and control signals
    .resetn   (pl_reset_n  ), // input reset
    .locked   (pll_locked      ), // output locked
    // Clock in ports
    .clk_in1  (ps2pl_clk50m_0  )  // input clk_in1
  );

  assign camera_xclk = loc_clk24m;
  
    camera_init
  #(
    .CAMERA_TYPE    ( "ov5640"     ),//"ov5640" or "ov7725"
    .IMAGE_TYPE     ( 0            ),// 0: RGB; 1: JPEG
    .IMAGE_WIDTH    ( IMAGE_WIDTH  ),// 图片宽度
    .IMAGE_HEIGHT   ( IMAGE_HEIGHT ),// 图片高度
    .IMAGE_FLIP_EN  ( 0            ),// 0: 不翻转，1: 上下翻转
    .IMAGE_MIRROR_EN( 0            ) // 0: 不镜像，1: 左右镜像
  )camera_init
  (
    .Clk         (loc_clk50m        ),
    .Rst_n       (!ref_reset      ),
    .Init_Done   ( camera_init_done ),
    .camera_rst_n(                  ),
    .camera_pwdn (                  ),
    .i2c_sclk    (camera_sclk       ),
    .i2c_sdat    (camera_sdat       )
  );

  BUFG BUFG_inst (
    .O(pclk_bufg_o ), // 1-bit output: Clock output
    .I(camera_pclk )  // 1-bit input: Clock input
  );

`ifdef USE_TEST_DATA
//---------------------------------------------
//产生测试数据
//---------------------------------------------
test_dat_gen #(
  .DISP_WIDTH  ( IMAGE_WIDTH  ),
  .DISP_HEIGHT ( IMAGE_HEIGHT ),
  .DATA_WIDTH  ( 16           )
)test_dat_gen_inst
(
  .clk            (pclk_bufg_o      ),
  .reset          (reset            ),
  .gen_en         (camera_init_done ),
  .test_dat       (image_data       ),
  .test_dat_vaild (image_data_valid )
);
//-------------------------------------------

`else
//---------------------------------------------
//摄像头采集数据
//---------------------------------------------
  DVP_Capture DVP_Capture(
    .Rst_p      (cam_reset        ),//input
    .PCLK       (pclk_bufg_o      ),//input
    .Vsync      (camera_vsync     ),//input
    .Href       (camera_href      ),//input
    .Data       (camera_data      ),//input     [7:0]

    .ImageState (                 ),//output reg
    .DataValid  (image_data_valid ),//output
    .DataPixel  (image_data       ),//output    [15:0]
    .DataHs     (image_data_hs    ),//output
    .DataVs     (image_data_vs    ),//output
    .Xaddr      (                 ),//output    [11:0]
    .Yaddr      (                 ) //output    [11:0]
  );
//---------------------------------------------
`endif

  wire video_pixel_valid;
  wire [31:0] video_status,video_complete,video_reject,video_repeat;
  wire [31:0] video_supersede,video_underflow,video_errors,video_seq;

  //显示模块
  disp_driver disp_driver(
    .ClkDisp     (disp_clk       ),
    .Rst_p       (disp_reset     ),

    .Data        (disp_data      ),
    .DataReq     (disp_data_req  ),

    .H_Addr      (               ),
    .V_Addr      (               ),

    .Disp_HS     (raw_hs         ),
    .Disp_VS     (raw_vs         ),
    .Disp_Red    (Disp_Red ),
    .Disp_Green  (Disp_Green  ),
    .Disp_Blue   (Disp_Blue   ),
    .Frame_Begin (frame_begin    ),
    .Disp_DE     (raw_de         ),
    .Disp_PCLK   (TFT_clk        )
  );
  

  // Vendor RGB registers lag its DE/HS/VS by one pixel. Align timing to RGB,
  // then count active pixels. Integration TB must check the actual vendor path.
  reg aligned_de,aligned_hs,aligned_vs;
  reg de_previous;
  reg [11:0] coord_x,coord_y;
  always @(posedge disp_clk or posedge disp_reset) begin
    if(disp_reset)begin aligned_de<=0;aligned_hs<=0;aligned_vs<=0;end
    else begin aligned_de<=raw_de;aligned_hs<=raw_hs;aligned_vs<=raw_vs;end
  end
  always @(posedge disp_clk or posedge disp_reset) begin
    if(disp_reset)begin coord_x<=0;coord_y<=0;de_previous<=0;end
    else begin
      de_previous<=aligned_de;
      if(frame_begin)begin coord_x<=0;coord_y<=0;end
      else if(aligned_de)coord_x<=coord_x+1;
      else if(de_previous)begin coord_x<=0;coord_y<=coord_y+1;end
    end
  end
  wire [23:0] raw_rgb888={Disp_Red,Disp_Red[4:2],Disp_Green,Disp_Green[5:4],Disp_Blue,Disp_Blue[4:2]};
  wire [23:0] marked_rgb;
  // 导出命令先跨域取得稳定引用；CPU复制后按令牌归还。
  wire acquire_valid,acquire_ready,export_valid,release_valid,release_accepted,token_exhausted;
  wire [1:0] export_bank;wire [31:0] export_seq,export_token,release_token;
  wire [7:0] legacy_awaddr,legacy_araddr;wire [31:0] legacy_wdata,legacy_rdata;
  wire [3:0] legacy_wstrb;wire [1:0] legacy_bresp,legacy_rresp;
  wire legacy_awvalid,legacy_awready,legacy_wvalid,legacy_wready,legacy_bvalid,legacy_bready;
  wire legacy_arvalid,legacy_arready,legacy_rvalid,legacy_rready;
  mic16_video_export_regs #(.BASE(DDR_BASE_ADDR),.WIDTH(IMAGE_WIDTH),.HEIGHT(IMAGE_HEIGHT)) export_control(
    .reset(control_reset),.ctrl_clk(ctrl_clk),.video_clk(loc_clk100m),
    .awaddr(s_axi_awaddr),
    .awvalid(s_axi_awvalid),
    .awready(s_axi_awready),
    .wdata(s_axi_wdata),
    .wstrb(s_axi_wstrb),
    .wvalid(s_axi_wvalid),
    .wready(s_axi_wready),
    .bresp(s_axi_bresp),
    .bvalid(s_axi_bvalid),
    .bready(s_axi_bready),
    .araddr(s_axi_araddr),
    .arvalid(s_axi_arvalid),
    .arready(s_axi_arready),
    .rdata(s_axi_rdata),
    .rresp(s_axi_rresp),
    .rvalid(s_axi_rvalid),
    .rready(s_axi_rready),
    .legacy_awaddr(legacy_awaddr),
    .legacy_awvalid(legacy_awvalid),
    .legacy_awready(legacy_awready),
    .legacy_wdata(legacy_wdata),
    .legacy_wstrb(legacy_wstrb),
    .legacy_wvalid(legacy_wvalid),
    .legacy_wready(legacy_wready),
    .legacy_bresp(legacy_bresp),
    .legacy_bvalid(legacy_bvalid),
    .legacy_bready(legacy_bready),
    .legacy_araddr(legacy_araddr),
    .legacy_arvalid(legacy_arvalid),
    .legacy_arready(legacy_arready),
    .legacy_rdata(legacy_rdata),
    .legacy_rresp(legacy_rresp),
    .legacy_rvalid(legacy_rvalid),
    .legacy_rready(legacy_rready),
    .acquire_valid(acquire_valid),
    .acquire_ready(acquire_ready),
    .export_valid(export_valid),
    .export_bank(export_bank),
    .export_seq(export_seq),
    .export_token(export_token),
    .release_valid(release_valid),
    .release_token(release_token),
    .release_accepted(release_accepted),
    .token_exhausted(token_exhausted));
  mic16_av_overlay_video #(.IMAGE_WIDTH(800),.IMAGE_HEIGHT(480),.PIX_CLK_HZ(32727273)) overlay(
    .s_axi_aclk(ctrl_clk),.pix_clk(disp_clk),.resetn(!control_reset),
    .video_clk(loc_clk100m),.video_status(video_status),.config_status(config_status),.video_complete(video_complete),
    .video_reject(video_reject),.video_repeat(video_repeat),.video_supersede(video_supersede),
    .video_underflow(video_underflow),.video_errors(video_errors),.video_seq(video_seq),
    .frame_start(frame_begin),.pixel_x(coord_x),.pixel_y(coord_y),
    .in_de(aligned_de),.in_hs(aligned_hs),.in_vs(aligned_vs),.in_rgb(raw_rgb888),
    .out_de(TFT_de),.out_hs(TFT_hs),.out_vs(TFT_vs),.out_rgb(marked_rgb),
    .active_cycle_id(),.marker_visible(),
    .s_axi_awaddr(legacy_awaddr),
    .s_axi_awprot(3'd0),
    .s_axi_awvalid(legacy_awvalid),
    .s_axi_awready(legacy_awready),
    .s_axi_wdata(legacy_wdata),
    .s_axi_wstrb(legacy_wstrb),
    .s_axi_wvalid(legacy_wvalid),
    .s_axi_wready(legacy_wready),
    .s_axi_bresp(legacy_bresp),
    .s_axi_bvalid(legacy_bvalid),
    .s_axi_bready(legacy_bready),
    .s_axi_araddr(legacy_araddr),
    .s_axi_arprot(3'd0),
    .s_axi_arvalid(legacy_arvalid),
    .s_axi_arready(legacy_arready),
    .s_axi_rdata(legacy_rdata),
    .s_axi_rresp(legacy_rresp),
    .s_axi_rvalid(legacy_rvalid),
    .s_axi_rready(legacy_rready));
  // Match the vendor RGB register plus overlay register; invalid video hides
  // both background and marker. HDMI timing still runs during black frames.
  reg video_valid_d1,video_valid_d2;
  always @(posedge disp_clk or posedge disp_reset)begin
    if(disp_reset)begin video_valid_d1<=0;video_valid_d2<=0;end
    else begin video_valid_d1<=video_pixel_valid;video_valid_d2<=video_valid_d1;end
  end
  assign TFT_rgb=video_valid_d2?{marked_rgb[23:19],marked_rgb[15:10],marked_rgb[7:3]}:16'd0;

  assign TFT_pwm = 1'b1;


  // Fixed 64-bit INCR bursts; DDR [0x10000000,0x10240000) reserved for video.
  assign m_video_awlock=0;assign m_video_awcache=0;assign m_video_awprot=0;
  assign m_video_awqos=0;assign m_video_awregion=0;
  assign m_video_arlock=0;assign m_video_arcache=0;assign m_video_arprot=0;
  assign m_video_arqos=0;assign m_video_arregion=0;
  mic16_video_frame_store #(.FRAME_PIXELS(IMAGE_WIDTH*IMAGE_HEIGHT),
    .BASE(DDR_BASE_ADDR),.STRIDE(32'h000c0000),.ENABLE_EXPORT(1)) frame_store(
    .clk(loc_clk100m),.reset(reset),.cam_clk(pclk_bufg_o),.pix_clk(disp_clk),
    .cam_frame_active(image_data_vs),.cam_valid(image_data_valid),.cam_data(image_data),
    .frame_start(frame_begin),.pixel_request(disp_data_req),
    .pixel_data(disp_data),.pixel_valid(video_pixel_valid),.pixel_frame_seq(),
    .completed_frames(video_complete),.rejected_frames(video_reject),.repeated_frames(video_repeat),
    .superseded_frames(video_supersede),.underflow_frames(video_underflow),.axi_errors(video_errors),
    .ownership(video_status),.display_sequence(video_seq),
    .acquire_valid(acquire_valid),
    .acquire_ready(acquire_ready),
    .export_valid(export_valid),
    .export_bank(export_bank),
    .export_seq(export_seq),
    .export_token(export_token),
    .release_valid(release_valid),
    .release_token(release_token),
    .release_accepted(release_accepted),
    .token_exhausted(token_exhausted),
    .awaddr(m_video_awaddr),
    .awlen(m_video_awlen),
    .awsize(m_video_awsize),
    .awburst(m_video_awburst),
    .awvalid(m_video_awvalid),
    .awready(m_video_awready),
    .wdata(m_video_wdata),
    .wstrb(m_video_wstrb),
    .wlast(m_video_wlast),
    .wvalid(m_video_wvalid),
    .wready(m_video_wready),
    .bresp(m_video_bresp),
    .bvalid(m_video_bvalid),
    .bready(m_video_bready),
    .araddr(m_video_araddr),
    .arlen(m_video_arlen),
    .arsize(m_video_arsize),
    .arburst(m_video_arburst),
    .arvalid(m_video_arvalid),
    .arready(m_video_arready),
    .rdata(m_video_rdata),
    .rresp(m_video_rresp),
    .rlast(m_video_rlast),
    .rvalid(m_video_rvalid),
    .rready(m_video_rready));

  reg [20:0]cnt;
  reg Go;
    
  always@(posedge ps2pl_clk50m_0 or posedge ref_reset)
  if(ref_reset)
      cnt <= 0;
  else if(cnt <= 499999)
      cnt <= cnt + 1 ;
  else
      cnt <= 500001;
        
        
  always@(posedge ps2pl_clk50m_0 or posedge ref_reset)
  if(ref_reset)
      Go <= 0;
  else if(cnt == 499999)
      Go <= 1'b1;
  else
      Go <= 0;
    
  SiI9022_Init SiI9022_Init(
      .Clk(ps2pl_clk50m_0),
      .Rst_n(!ref_reset),
        
      .Go(Go),
      .device_id(8'h72),
      .Init_Done(hdmi_init_done),
        
      .i2c_sclk(SiI9022_sclk),
      .i2c_sdat(SiI9022_sdat)
  );

endmodule
