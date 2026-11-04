//Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
//--------------------------------------------------------------------------------
//Tool Version: Vivado v.2020.2 (win64) Build 3064766 Wed Nov 18 09:12:45 MST 2020
//Date        : Sun Oct 18 12:49:58 2026
//Host        : LAPTOP-VOLSUJSE running 64-bit major release  (build 9200)
//Command     : generate_target bd_mic16_wrapper.bd
//Design      : bd_mic16_wrapper
//Purpose     : IP block netlist
//--------------------------------------------------------------------------------
`timescale 1 ps / 1 ps

module bd_mic16_wrapper
   (DDR_addr,
    DDR_ba,
    DDR_cas_n,
    DDR_ck_n,
    DDR_ck_p,
    DDR_cke,
    DDR_cs_n,
    DDR_dm,
    DDR_dq,
    DDR_dqs_n,
    DDR_dqs_p,
    DDR_odt,
    DDR_ras_n,
    DDR_reset_n,
    DDR_we_n,
    FIXED_IO_ddr_vrn,
    FIXED_IO_ddr_vrp,
    FIXED_IO_mio,
    FIXED_IO_ps_clk,
    FIXED_IO_ps_porb,
    FIXED_IO_ps_srstb,
    GPIO2_0_tri_o,
    GPIO_0_tri_i,
    SiI9022_sclk,
    SiI9022_sdat,
    TFT_clk,
    TFT_de,
    TFT_hs,
    TFT_pwm,
    TFT_rgb,
    TFT_vs,
    camera_data,
    camera_href,
    camera_pclk,
    camera_sclk,
    camera_sdat,
    camera_vsync,
    camera_xclk,
    pdm_clk_out_0,
    pdm_data0_0,
    pdm_data10_0,
    pdm_data11_0,
    pdm_data12_0,
    pdm_data13_0,
    pdm_data14_0,
    pdm_data15_0,
    pdm_data1_0,
    pdm_data2_0,
    pdm_data3_0,
    pdm_data4_0,
    pdm_data5_0,
    pdm_data6_0,
    pdm_data7_0,
    pdm_data8_0,
    pdm_data9_0);
  inout [14:0]DDR_addr;
  inout [2:0]DDR_ba;
  inout DDR_cas_n;
  inout DDR_ck_n;
  inout DDR_ck_p;
  inout DDR_cke;
  inout DDR_cs_n;
  inout [3:0]DDR_dm;
  inout [31:0]DDR_dq;
  inout [3:0]DDR_dqs_n;
  inout [3:0]DDR_dqs_p;
  inout DDR_odt;
  inout DDR_ras_n;
  inout DDR_reset_n;
  inout DDR_we_n;
  inout FIXED_IO_ddr_vrn;
  inout FIXED_IO_ddr_vrp;
  inout [53:0]FIXED_IO_mio;
  inout FIXED_IO_ps_clk;
  inout FIXED_IO_ps_porb;
  inout FIXED_IO_ps_srstb;
  output [2:0]GPIO2_0_tri_o;
  input [1:0]GPIO_0_tri_i;
  output SiI9022_sclk;
  inout SiI9022_sdat;
  output TFT_clk;
  output TFT_de;
  output TFT_hs;
  output TFT_pwm;
  output [15:0]TFT_rgb;
  output TFT_vs;
  input [7:0]camera_data;
  input camera_href;
  input camera_pclk;
  output camera_sclk;
  inout camera_sdat;
  input camera_vsync;
  output camera_xclk;
  output [0:0]pdm_clk_out_0;
  input pdm_data0_0;
  input pdm_data10_0;
  input pdm_data11_0;
  input pdm_data12_0;
  input pdm_data13_0;
  input pdm_data14_0;
  input pdm_data15_0;
  input pdm_data1_0;
  input pdm_data2_0;
  input pdm_data3_0;
  input pdm_data4_0;
  input pdm_data5_0;
  input pdm_data6_0;
  input pdm_data7_0;
  input pdm_data8_0;
  input pdm_data9_0;

  wire [14:0]DDR_addr;
  wire [2:0]DDR_ba;
  wire DDR_cas_n;
  wire DDR_ck_n;
  wire DDR_ck_p;
  wire DDR_cke;
  wire DDR_cs_n;
  wire [3:0]DDR_dm;
  wire [31:0]DDR_dq;
  wire [3:0]DDR_dqs_n;
  wire [3:0]DDR_dqs_p;
  wire DDR_odt;
  wire DDR_ras_n;
  wire DDR_reset_n;
  wire DDR_we_n;
  wire FIXED_IO_ddr_vrn;
  wire FIXED_IO_ddr_vrp;
  wire [53:0]FIXED_IO_mio;
  wire FIXED_IO_ps_clk;
  wire FIXED_IO_ps_porb;
  wire FIXED_IO_ps_srstb;
  wire [2:0]GPIO2_0_tri_o;
  wire [1:0]GPIO_0_tri_i;
  wire SiI9022_sclk;
  wire SiI9022_sdat;
  wire TFT_clk;
  wire TFT_de;
  wire TFT_hs;
  wire TFT_pwm;
  wire [15:0]TFT_rgb;
  wire TFT_vs;
  wire [7:0]camera_data;
  wire camera_href;
  wire camera_pclk;
  wire camera_sclk;
  wire camera_sdat;
  wire camera_vsync;
  wire camera_xclk;
  wire [0:0]pdm_clk_out_0;
  wire pdm_data0_0;
  wire pdm_data10_0;
  wire pdm_data11_0;
  wire pdm_data12_0;
  wire pdm_data13_0;
  wire pdm_data14_0;
  wire pdm_data15_0;
  wire pdm_data1_0;
  wire pdm_data2_0;
  wire pdm_data3_0;
  wire pdm_data4_0;
  wire pdm_data5_0;
  wire pdm_data6_0;
  wire pdm_data7_0;
  wire pdm_data8_0;
  wire pdm_data9_0;

  bd_mic16 bd_mic16_i
       (.DDR_addr(DDR_addr),
        .DDR_ba(DDR_ba),
        .DDR_cas_n(DDR_cas_n),
        .DDR_ck_n(DDR_ck_n),
        .DDR_ck_p(DDR_ck_p),
        .DDR_cke(DDR_cke),
        .DDR_cs_n(DDR_cs_n),
        .DDR_dm(DDR_dm),
        .DDR_dq(DDR_dq),
        .DDR_dqs_n(DDR_dqs_n),
        .DDR_dqs_p(DDR_dqs_p),
        .DDR_odt(DDR_odt),
        .DDR_ras_n(DDR_ras_n),
        .DDR_reset_n(DDR_reset_n),
        .DDR_we_n(DDR_we_n),
        .FIXED_IO_ddr_vrn(FIXED_IO_ddr_vrn),
        .FIXED_IO_ddr_vrp(FIXED_IO_ddr_vrp),
        .FIXED_IO_mio(FIXED_IO_mio),
        .FIXED_IO_ps_clk(FIXED_IO_ps_clk),
        .FIXED_IO_ps_porb(FIXED_IO_ps_porb),
        .FIXED_IO_ps_srstb(FIXED_IO_ps_srstb),
        .GPIO2_0_tri_o(GPIO2_0_tri_o),
        .GPIO_0_tri_i(GPIO_0_tri_i),
        .SiI9022_sclk(SiI9022_sclk),
        .SiI9022_sdat(SiI9022_sdat),
        .TFT_clk(TFT_clk),
        .TFT_de(TFT_de),
        .TFT_hs(TFT_hs),
        .TFT_pwm(TFT_pwm),
        .TFT_rgb(TFT_rgb),
        .TFT_vs(TFT_vs),
        .camera_data(camera_data),
        .camera_href(camera_href),
        .camera_pclk(camera_pclk),
        .camera_sclk(camera_sclk),
        .camera_sdat(camera_sdat),
        .camera_vsync(camera_vsync),
        .camera_xclk(camera_xclk),
        .pdm_clk_out_0(pdm_clk_out_0),
        .pdm_data0_0(pdm_data0_0),
        .pdm_data10_0(pdm_data10_0),
        .pdm_data11_0(pdm_data11_0),
        .pdm_data12_0(pdm_data12_0),
        .pdm_data13_0(pdm_data13_0),
        .pdm_data14_0(pdm_data14_0),
        .pdm_data15_0(pdm_data15_0),
        .pdm_data1_0(pdm_data1_0),
        .pdm_data2_0(pdm_data2_0),
        .pdm_data3_0(pdm_data3_0),
        .pdm_data4_0(pdm_data4_0),
        .pdm_data5_0(pdm_data5_0),
        .pdm_data6_0(pdm_data6_0),
        .pdm_data7_0(pdm_data7_0),
        .pdm_data8_0(pdm_data8_0),
        .pdm_data9_0(pdm_data9_0));
endmodule
