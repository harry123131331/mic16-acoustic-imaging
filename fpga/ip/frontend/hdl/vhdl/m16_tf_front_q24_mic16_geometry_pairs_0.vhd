-- ==============================================================
-- Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
-- Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
-- ==============================================================
library ieee; 
use ieee.std_logic_1164.all; 
use ieee.std_logic_unsigned.all;

entity m16_tf_front_q24_MIC16_GEOMETRY_PAIRS_0_rom is 
    generic(
             DWIDTH     : integer := 4; 
             AWIDTH     : integer := 5; 
             MEM_SIZE    : integer := 32
    ); 
    port (
          addr0      : in std_logic_vector(AWIDTH-1 downto 0); 
          ce0       : in std_logic; 
          q0         : out std_logic_vector(DWIDTH-1 downto 0);
          clk       : in std_logic
    ); 
end entity; 


architecture rtl of m16_tf_front_q24_MIC16_GEOMETRY_PAIRS_0_rom is 

signal addr0_tmp : std_logic_vector(AWIDTH-1 downto 0); 
type mem_array is array (0 to MEM_SIZE-1) of std_logic_vector (DWIDTH-1 downto 0); 
signal mem : mem_array := (
    0 => "0000", 1 => "0001", 2 => "0010", 3 => "0011", 4 => "0100", 
    5 => "0101", 6 => "0110", 7 => "0111", 8 => "1000", 9 => "1001", 
    10 => "1010", 11 => "1011", 12 => "1100", 13 => "1101", 14 => "1110", 
    15 => "1111", 16 => "0000", 17 => "0001", 18 => "0010", 19 => "0011", 
    20 => "0100", 21 => "0101", 22 => "0110", 23 => "0111", 24 => "0000", 
    25 => "0001", 26 => "0010", 27 => "0011", 28 => "0100", 29 => "0101", 
    30 => "0110", 31 => "0111" );


begin 


memory_access_guard_0: process (addr0) 
begin
      addr0_tmp <= addr0;
--synthesis translate_off
      if (CONV_INTEGER(addr0) > mem_size-1) then
           addr0_tmp <= (others => '0');
      else 
           addr0_tmp <= addr0;
      end if;
--synthesis translate_on
end process;

p_rom_access: process (clk)  
begin 
    if (clk'event and clk = '1') then
        if (ce0 = '1') then 
            q0 <= mem(CONV_INTEGER(addr0_tmp)); 
        end if;
    end if;
end process;

end rtl;

Library IEEE;
use IEEE.std_logic_1164.all;

entity m16_tf_front_q24_MIC16_GEOMETRY_PAIRS_0 is
    generic (
        DataWidth : INTEGER := 4;
        AddressRange : INTEGER := 32;
        AddressWidth : INTEGER := 5);
    port (
        reset : IN STD_LOGIC;
        clk : IN STD_LOGIC;
        address0 : IN STD_LOGIC_VECTOR(AddressWidth - 1 DOWNTO 0);
        ce0 : IN STD_LOGIC;
        q0 : OUT STD_LOGIC_VECTOR(DataWidth - 1 DOWNTO 0));
end entity;

architecture arch of m16_tf_front_q24_MIC16_GEOMETRY_PAIRS_0 is
    component m16_tf_front_q24_MIC16_GEOMETRY_PAIRS_0_rom is
        port (
            clk : IN STD_LOGIC;
            addr0 : IN STD_LOGIC_VECTOR;
            ce0 : IN STD_LOGIC;
            q0 : OUT STD_LOGIC_VECTOR);
    end component;



begin
    m16_tf_front_q24_MIC16_GEOMETRY_PAIRS_0_rom_U :  component m16_tf_front_q24_MIC16_GEOMETRY_PAIRS_0_rom
    port map (
        clk => clk,
        addr0 => address0,
        ce0 => ce0,
        q0 => q0);

end architecture;


