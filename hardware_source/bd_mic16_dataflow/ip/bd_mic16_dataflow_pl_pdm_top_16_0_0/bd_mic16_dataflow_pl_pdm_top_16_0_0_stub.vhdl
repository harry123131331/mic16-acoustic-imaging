-- Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
-- --------------------------------------------------------------------------------
-- Tool Version: Vivado v.2020.2 (win64) Build 3064766 Wed Nov 18 09:12:45 MST 2020
-- Date        : Tue Oct 27 18:46:08 2026
-- Host        : LAPTOP-VOLSUJSE running 64-bit major release  (build 9200)
-- Command     : write_vhdl -force -mode synth_stub
--               m:/build/hardware/dataflow_architecture_20261024/source/bd_mic16_dataflow/ip/bd_mic16_dataflow_pl_pdm_top_16_0_0/bd_mic16_dataflow_pl_pdm_top_16_0_0_stub.vhdl
-- Design      : bd_mic16_dataflow_pl_pdm_top_16_0_0
-- Purpose     : Stub declaration of top-level module interface
-- Device      : xc7z020clg484-2
-- --------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

entity bd_mic16_dataflow_pl_pdm_top_16_0_0 is
  Port ( 
    clk_ref : in STD_LOGIC;
    rst_n : in STD_LOGIC;
    enable : in STD_LOGIC;
    pdm_clk_div : in STD_LOGIC_VECTOR ( 15 downto 0 );
    pdm_data0 : in STD_LOGIC;
    pdm_data1 : in STD_LOGIC;
    pdm_data2 : in STD_LOGIC;
    pdm_data3 : in STD_LOGIC;
    pdm_data4 : in STD_LOGIC;
    pdm_data5 : in STD_LOGIC;
    pdm_data6 : in STD_LOGIC;
    pdm_data7 : in STD_LOGIC;
    pdm_data8 : in STD_LOGIC;
    pdm_data9 : in STD_LOGIC;
    pdm_data10 : in STD_LOGIC;
    pdm_data11 : in STD_LOGIC;
    pdm_data12 : in STD_LOGIC;
    pdm_data13 : in STD_LOGIC;
    pdm_data14 : in STD_LOGIC;
    pdm_data15 : in STD_LOGIC;
    pdm_clk_out : out STD_LOGIC;
    m_axis_tdata : out STD_LOGIC_VECTOR ( 31 downto 0 );
    m_axis_tkeep : out STD_LOGIC_VECTOR ( 3 downto 0 );
    m_axis_tlast : out STD_LOGIC;
    m_axis_tvalid : out STD_LOGIC;
    m_axis_tready : in STD_LOGIC;
    dbg_channel : out STD_LOGIC_VECTOR ( 3 downto 0 );
    dbg_sample_index : out STD_LOGIC_VECTOR ( 15 downto 0 );
    dbg_fifo_level : out STD_LOGIC_VECTOR ( 15 downto 0 );
    pdm_clk_running : out STD_LOGIC;
    overflow_error : out STD_LOGIC;
    overflow_count : out STD_LOGIC_VECTOR ( 31 downto 0 )
  );

end bd_mic16_dataflow_pl_pdm_top_16_0_0;

architecture stub of bd_mic16_dataflow_pl_pdm_top_16_0_0 is
attribute syn_black_box : boolean;
attribute black_box_pad_pin : string;
attribute syn_black_box of stub : architecture is true;
attribute black_box_pad_pin of stub : architecture is "clk_ref,rst_n,enable,pdm_clk_div[15:0],pdm_data0,pdm_data1,pdm_data2,pdm_data3,pdm_data4,pdm_data5,pdm_data6,pdm_data7,pdm_data8,pdm_data9,pdm_data10,pdm_data11,pdm_data12,pdm_data13,pdm_data14,pdm_data15,pdm_clk_out,m_axis_tdata[31:0],m_axis_tkeep[3:0],m_axis_tlast,m_axis_tvalid,m_axis_tready,dbg_channel[3:0],dbg_sample_index[15:0],dbg_fifo_level[15:0],pdm_clk_running,overflow_error,overflow_count[31:0]";
attribute X_CORE_INFO : string;
attribute X_CORE_INFO of stub : architecture is "pl_pdm_top_16,Vivado 2020.2";
begin
end;
