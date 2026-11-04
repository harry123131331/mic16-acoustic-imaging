-- Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
-- --------------------------------------------------------------------------------
-- Tool Version: Vivado v.2020.2 (win64) Build 3064766 Wed Nov 18 09:12:45 MST 2020
-- Date        : Sat Oct 24 22:03:01 2026
-- Host        : LAPTOP-VOLSUJSE running 64-bit major release  (build 9200)
-- Command     : write_vhdl -force -mode synth_stub
--               m:/build/hardware/dataflow_architecture_20261024/source/bd_mic16_dataflow/ip/bd_mic16_dataflow_clk_wiz_0_0/bd_mic16_dataflow_clk_wiz_0_0_stub.vhdl
-- Design      : bd_mic16_dataflow_clk_wiz_0_0
-- Purpose     : Stub declaration of top-level module interface
-- Device      : xc7z020clg484-2
-- --------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

entity bd_mic16_dataflow_clk_wiz_0_0 is
  Port ( 
    clk_out1 : out STD_LOGIC;
    reset : in STD_LOGIC;
    locked : out STD_LOGIC;
    clk_in1 : in STD_LOGIC
  );

end bd_mic16_dataflow_clk_wiz_0_0;

architecture stub of bd_mic16_dataflow_clk_wiz_0_0 is
attribute syn_black_box : boolean;
attribute black_box_pad_pin : string;
attribute syn_black_box of stub : architecture is true;
attribute black_box_pad_pin of stub : architecture is "clk_out1,reset,locked,clk_in1";
begin
end;
