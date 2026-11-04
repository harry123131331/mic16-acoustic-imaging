-- Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
-- --------------------------------------------------------------------------------
-- Tool Version: Vivado v.2020.2 (win64) Build 3064766 Wed Nov 18 09:12:45 MST 2020
-- Date        : Sun Oct 25 09:24:36 2026
-- Host        : LAPTOP-VOLSUJSE running 64-bit major release  (build 9200)
-- Command     : write_vhdl -force -mode synth_stub
--               m:/build/hardware/dataflow_architecture_20261024/source/bd_mic16_dataflow/ip/bd_mic16_dataflow_audio_fifo_reset_gate_0/bd_mic16_dataflow_audio_fifo_reset_gate_0_stub.vhdl
-- Design      : bd_mic16_dataflow_audio_fifo_reset_gate_0
-- Purpose     : Stub declaration of top-level module interface
-- Device      : xc7z020clg484-2
-- --------------------------------------------------------------------------------
library IEEE;
use IEEE.STD_LOGIC_1164.ALL;

entity bd_mic16_dataflow_audio_fifo_reset_gate_0 is
  Port ( 
    Op1 : in STD_LOGIC_VECTOR ( 0 to 0 );
    Op2 : in STD_LOGIC_VECTOR ( 0 to 0 );
    Res : out STD_LOGIC_VECTOR ( 0 to 0 )
  );

end bd_mic16_dataflow_audio_fifo_reset_gate_0;

architecture stub of bd_mic16_dataflow_audio_fifo_reset_gate_0 is
attribute syn_black_box : boolean;
attribute black_box_pad_pin : string;
attribute syn_black_box of stub : architecture is true;
attribute black_box_pad_pin of stub : architecture is "Op1[0:0],Op2[0:0],Res[0:0]";
attribute X_CORE_INFO : string;
attribute X_CORE_INFO of stub : architecture is "util_vector_logic_v2_0_1_util_vector_logic,Vivado 2020.2";
begin
end;
