-- ==============================================================
-- Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
-- Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
-- ==============================================================
library ieee; 
use ieee.std_logic_1164.all; 
use ieee.std_logic_unsigned.all;

entity m16_sp_shared_MIC16_GEOMETRY_PAIRS_1_rom is 
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


architecture rtl of m16_sp_shared_MIC16_GEOMETRY_PAIRS_1_rom is 

signal addr0_tmp : std_logic_vector(AWIDTH-1 downto 0); 
type mem_array is array (0 to MEM_SIZE-1) of std_logic_vector (DWIDTH-1 downto 0); 
signal mem : mem_array := (
    0 => "0001", 1 => "0010", 2 => "0011", 3 => "0100", 4 => "0101", 
    5 => "0110", 6 => "0111", 7 => "1000", 8 => "1001", 9 => "1010", 
    10 => "1011", 11 => "1100", 12 => "1101", 13 => "1110", 14 => "1111", 
    15 => "0000", 16 => "1000", 17 => "1001", 18 => "1010", 19 => "1011", 
    20 => "1100", 21 => "1101", 22 => "1110", 23 => "1111", 24 => "0100", 
    25 => "0101", 26 => "0110", 27 => "0111", 28 => "1000", 29 => "1001", 
    30 => "1010", 31 => "1011" );


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

entity m16_sp_shared_MIC16_GEOMETRY_PAIRS_1 is
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

architecture arch of m16_sp_shared_MIC16_GEOMETRY_PAIRS_1 is
    component m16_sp_shared_MIC16_GEOMETRY_PAIRS_1_rom is
        port (
            clk : IN STD_LOGIC;
            addr0 : IN STD_LOGIC_VECTOR;
            ce0 : IN STD_LOGIC;
            q0 : OUT STD_LOGIC_VECTOR);
    end component;



begin
    m16_sp_shared_MIC16_GEOMETRY_PAIRS_1_rom_U :  component m16_sp_shared_MIC16_GEOMETRY_PAIRS_1_rom
    port map (
        clk => clk,
        addr0 => address0,
        ce0 => ce0,
        q0 => q0);

end architecture;


