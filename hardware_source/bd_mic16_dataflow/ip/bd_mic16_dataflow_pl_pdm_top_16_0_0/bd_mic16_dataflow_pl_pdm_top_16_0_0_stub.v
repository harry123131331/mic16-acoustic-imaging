// Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
// --------------------------------------------------------------------------------
// Tool Version: Vivado v.2020.2 (win64) Build 3064766 Wed Nov 18 09:12:45 MST 2020
// Date        : Tue Oct 27 18:46:08 2026
// Host        : LAPTOP-VOLSUJSE running 64-bit major release  (build 9200)
// Command     : write_verilog -force -mode synth_stub
//               m:/build/hardware/dataflow_architecture_20261024/source/bd_mic16_dataflow/ip/bd_mic16_dataflow_pl_pdm_top_16_0_0/bd_mic16_dataflow_pl_pdm_top_16_0_0_stub.v
// Design      : bd_mic16_dataflow_pl_pdm_top_16_0_0
// Purpose     : Stub declaration of top-level module interface
// Device      : xc7z020clg484-2
// --------------------------------------------------------------------------------

// This empty module with port declaration file causes synthesis tools to infer a black box for IP.
// The synthesis directives are for Synopsys Synplify support to prevent IO buffer insertion.
// Please paste the declaration into a Verilog source file or add the file as an additional source.
(* X_CORE_INFO = "pl_pdm_top_16,Vivado 2020.2" *)
module bd_mic16_dataflow_pl_pdm_top_16_0_0(clk_ref, rst_n, enable, pdm_clk_div, pdm_data0, 
  pdm_data1, pdm_data2, pdm_data3, pdm_data4, pdm_data5, pdm_data6, pdm_data7, pdm_data8, 
  pdm_data9, pdm_data10, pdm_data11, pdm_data12, pdm_data13, pdm_data14, pdm_data15, pdm_clk_out, 
  m_axis_tdata, m_axis_tkeep, m_axis_tlast, m_axis_tvalid, m_axis_tready, dbg_channel, 
  dbg_sample_index, dbg_fifo_level, pdm_clk_running, overflow_error, overflow_count)
/* synthesis syn_black_box black_box_pad_pin="clk_ref,rst_n,enable,pdm_clk_div[15:0],pdm_data0,pdm_data1,pdm_data2,pdm_data3,pdm_data4,pdm_data5,pdm_data6,pdm_data7,pdm_data8,pdm_data9,pdm_data10,pdm_data11,pdm_data12,pdm_data13,pdm_data14,pdm_data15,pdm_clk_out,m_axis_tdata[31:0],m_axis_tkeep[3:0],m_axis_tlast,m_axis_tvalid,m_axis_tready,dbg_channel[3:0],dbg_sample_index[15:0],dbg_fifo_level[15:0],pdm_clk_running,overflow_error,overflow_count[31:0]" */;
  input clk_ref;
  input rst_n;
  input enable;
  input [15:0]pdm_clk_div;
  input pdm_data0;
  input pdm_data1;
  input pdm_data2;
  input pdm_data3;
  input pdm_data4;
  input pdm_data5;
  input pdm_data6;
  input pdm_data7;
  input pdm_data8;
  input pdm_data9;
  input pdm_data10;
  input pdm_data11;
  input pdm_data12;
  input pdm_data13;
  input pdm_data14;
  input pdm_data15;
  output pdm_clk_out;
  output [31:0]m_axis_tdata;
  output [3:0]m_axis_tkeep;
  output m_axis_tlast;
  output m_axis_tvalid;
  input m_axis_tready;
  output [3:0]dbg_channel;
  output [15:0]dbg_sample_index;
  output [15:0]dbg_fifo_level;
  output pdm_clk_running;
  output overflow_error;
  output [31:0]overflow_count;
endmodule
