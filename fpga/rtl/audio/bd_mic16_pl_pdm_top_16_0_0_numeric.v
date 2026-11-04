// (c) Copyright 1995-2026 Xilinx, Inc. All rights reserved.
// 
// This file contains confidential and proprietary information
// of Xilinx, Inc. and is protected under U.S. and
// international copyright and other intellectual property
// laws.
// 
// DISCLAIMER
// This disclaimer is not a license and does not grant any
// rights to the materials distributed herewith. Except as
// otherwise provided in a valid license issued to you by
// Xilinx, and to the maximum extent permitted by applicable
// law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND
// WITH ALL FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES
// AND CONDITIONS, EXPRESS, IMPLIED, OR STATUTORY, INCLUDING
// BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, NON-
// INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE; and
// (2) Xilinx shall not be liable (whether in contract or tort,
// including negligence, or under any other theory of
// liability) for any loss or damage of any kind or nature
// related to, arising under or in connection with these
// materials, including for any direct, or any indirect,
// special, incidental, or consequential loss or damage
// (including loss of data, profits, goodwill, or any type of
// loss or damage suffered as a result of any action brought
// by a third party) even if such damage or loss was
// reasonably foreseeable or Xilinx had been advised of the
// possibility of the same.
// 
// CRITICAL APPLICATIONS
// Xilinx products are not designed or intended to be fail-
// safe, or for use in any application requiring fail-safe
// performance, such as life-support or safety devices or
// systems, Class III medical devices, nuclear facilities,
// applications related to the deployment of airbags, or any
// other applications that could lead to death, personal
// injury, or severe property or environmental damage
// (individually and collectively, "Critical
// Applications"). Customer assumes the sole risk and
// liability of any use of Xilinx products in Critical
// Applications, subject only to applicable laws and
// regulations governing limitations on product liability.
// 
// THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS
// PART OF THIS FILE AT ALL TIMES.
// 
// DO NOT MODIFY THIS FILE.


// IP VLNV: xilinx.com:module_ref:pl_pdm_top_16:1.0
// IP Revision: 1

(* X_CORE_INFO = "pl_pdm_top_16,Vivado 2020.2" *)
(* CHECK_LICENSE_TYPE = "bd_mic16_pl_pdm_top_16_0_0,pl_pdm_top_16,{}" *)
(* CORE_GENERATION_INFO = "bd_mic16_pl_pdm_top_16_0_0,pl_pdm_top_16,{x_ipProduct=Vivado 2020.2,x_ipVendor=xilinx.com,x_ipLibrary=module_ref,x_ipName=pl_pdm_top_16,x_ipVersion=1.0,x_ipCoreRevision=1,x_ipLanguage=VERILOG,x_ipSimLanguage=MIXED,AXIS_GROUP_FIFO_DEPTH=8,DC_BLOCK_SHIFT=10,DECIMATION=64,ENABLE_CIC_COMP=1,ENABLE_DC_BLOCK=1,ENABLE_POST_FIR=1,SAMPLES_PER_PACKET=16,SCALE_SHIFT=12}" *)
(* DowngradeIPIdentifiedWarnings = "yes" *)
module bd_mic16_pl_pdm_top_16_0_0_numeric (
  clk_ref,
  rst_n,
  enable,
  pdm_clk_div,
  pdm_data0,
  pdm_data1,
  pdm_data2,
  pdm_data3,
  pdm_data4,
  pdm_data5,
  pdm_data6,
  pdm_data7,
  pdm_data8,
  pdm_data9,
  pdm_data10,
  pdm_data11,
  pdm_data12,
  pdm_data13,
  pdm_data14,
  pdm_data15,
  pdm_clk_out,
  m_axis_tdata,
  m_axis_tkeep,
  m_axis_tlast,
  m_axis_tvalid,
  m_axis_tready,
  dbg_channel,
  dbg_sample_index,
  dbg_fifo_level,
  pdm_clk_running,
  overflow_error,
  overflow_count
);

(* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME clk_ref, ASSOCIATED_BUSIF m_axis, ASSOCIATED_RESET rst_n, FREQ_HZ 100000000, FREQ_TOLERANCE_HZ 0, PHASE 0.0, CLK_DOMAIN /clk_wiz_0_clk_out1, INSERT_VIP 0" *)
(* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk_ref CLK" *)
input wire [0 : 0] clk_ref;
(* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME rst_n, POLARITY ACTIVE_LOW, INSERT_VIP 0" *)
(* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 rst_n RST" *)
input wire [0 : 0] rst_n;
input wire [0 : 0] enable;
input wire [15 : 0] pdm_clk_div;
input wire [0 : 0] pdm_data0;
input wire [0 : 0] pdm_data1;
input wire [0 : 0] pdm_data2;
input wire [0 : 0] pdm_data3;
input wire [0 : 0] pdm_data4;
input wire [0 : 0] pdm_data5;
input wire [0 : 0] pdm_data6;
input wire [0 : 0] pdm_data7;
input wire [0 : 0] pdm_data8;
input wire [0 : 0] pdm_data9;
input wire [0 : 0] pdm_data10;
input wire [0 : 0] pdm_data11;
input wire [0 : 0] pdm_data12;
input wire [0 : 0] pdm_data13;
input wire [0 : 0] pdm_data14;
input wire [0 : 0] pdm_data15;
output wire [0 : 0] pdm_clk_out;
(* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TDATA" *)
output wire [31 : 0] m_axis_tdata;
(* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TKEEP" *)
output wire [3 : 0] m_axis_tkeep;
(* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TLAST" *)
output wire [0 : 0] m_axis_tlast;
(* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TVALID" *)
output wire [0 : 0] m_axis_tvalid;
(* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME m_axis, TDATA_NUM_BYTES 4, TDEST_WIDTH 0, TID_WIDTH 0, TUSER_WIDTH 0, HAS_TREADY 1, HAS_TSTRB 0, HAS_TKEEP 1, HAS_TLAST 1, FREQ_HZ 100000000, PHASE 0.0, CLK_DOMAIN /clk_wiz_0_clk_out1, LAYERED_METADATA undef, INSERT_VIP 0" *)
(* X_INTERFACE_INFO = "xilinx.com:interface:axis:1.0 m_axis TREADY" *)
input wire [0 : 0] m_axis_tready;
output wire [3 : 0] dbg_channel;
output wire [15 : 0] dbg_sample_index;
output wire [15 : 0] dbg_fifo_level;
output wire [0 : 0] pdm_clk_running;
output wire [0 : 0] overflow_error;
output wire [31 : 0] overflow_count;

  pl_pdm_top_16 #(
    .AXIS_GROUP_FIFO_DEPTH(8),
    .DC_BLOCK_SHIFT(10),
    .DECIMATION(64),
    .ENABLE_CIC_COMP(1),
    .ENABLE_DC_BLOCK(1),
    .ENABLE_POST_FIR(1),
    .SAMPLES_PER_PACKET(16),
    .SCALE_SHIFT(12)
  ) inst (
    .clk_ref(clk_ref),
    .rst_n(rst_n),
    .enable(enable),
    .pdm_clk_div(pdm_clk_div),
    .pdm_data0(pdm_data0),
    .pdm_data1(pdm_data1),
    .pdm_data2(pdm_data2),
    .pdm_data3(pdm_data3),
    .pdm_data4(pdm_data4),
    .pdm_data5(pdm_data5),
    .pdm_data6(pdm_data6),
    .pdm_data7(pdm_data7),
    .pdm_data8(pdm_data8),
    .pdm_data9(pdm_data9),
    .pdm_data10(pdm_data10),
    .pdm_data11(pdm_data11),
    .pdm_data12(pdm_data12),
    .pdm_data13(pdm_data13),
    .pdm_data14(pdm_data14),
    .pdm_data15(pdm_data15),
    .pdm_clk_out(pdm_clk_out),
    .m_axis_tdata(m_axis_tdata),
    .m_axis_tkeep(m_axis_tkeep),
    .m_axis_tlast(m_axis_tlast),
    .m_axis_tvalid(m_axis_tvalid),
    .m_axis_tready(m_axis_tready),
    .dbg_channel(dbg_channel),
    .dbg_sample_index(dbg_sample_index),
    .dbg_fifo_level(dbg_fifo_level),
    .pdm_clk_running(pdm_clk_running),
    .overflow_error(overflow_error),
    .overflow_count(overflow_count)
  );
endmodule
