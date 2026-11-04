-- ==============================================================
-- Vitis HLS - High-Level Synthesis from C, C++ and OpenCL v2020.2 (64-bit)
-- Copyright 1986-2020 Xilinx, Inc. All Rights Reserved.
-- ==============================================================
library ieee; 
use ieee.std_logic_1164.all; 
use ieee.std_logic_unsigned.all;

entity mic16_tf_zone_streaming_hls_q2_24_k_zone_offsets_q2_24_rom is 
    generic(
             DWIDTH     : integer := 10; 
             AWIDTH     : integer := 5; 
             MEM_SIZE    : integer := 25
    ); 
    port (
          addr0      : in std_logic_vector(AWIDTH-1 downto 0); 
          ce0       : in std_logic; 
          q0         : out std_logic_vector(DWIDTH-1 downto 0);
          addr1      : in std_logic_vector(AWIDTH-1 downto 0); 
          ce1       : in std_logic; 
          q1         : out std_logic_vector(DWIDTH-1 downto 0);
          clk       : in std_logic
    ); 
end entity; 


architecture rtl of mic16_tf_zone_streaming_hls_q2_24_k_zone_offsets_q2_24_rom is 

signal addr0_tmp : std_logic_vector(AWIDTH-1 downto 0); 
signal addr1_tmp : std_logic_vector(AWIDTH-1 downto 0); 
type mem_array is array (0 to MEM_SIZE-1) of std_logic_vector (DWIDTH-1 downto 0); 
signal mem : mem_array := (
    0 => "0000000000", 1 => "0000100010", 2 => "0001000100", 3 => "0001100110", 
    4 => "0010001000", 5 => "0010101010", 6 => "0011001100", 7 => "0011101110", 
    8 => "0100010000", 9 => "0100110010", 10 => "0101010100", 11 => "0101110110", 
    12 => "0110011000", 13 => "0110111010", 14 => "0111011100", 15 => "0111111110", 
    16 => "1000100000", 17 => "1001000010", 18 => "1001100100", 19 => "1010000110", 
    20 => "1010101000", 21 => "1011001010", 22 => "1011101100", 23 => "1100001110", 
    24 => "1100100111" );


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

memory_access_guard_1: process (addr1) 
begin
      addr1_tmp <= addr1;
--synthesis translate_off
      if (CONV_INTEGER(addr1) > mem_size-1) then
           addr1_tmp <= (others => '0');
      else 
           addr1_tmp <= addr1;
      end if;
--synthesis translate_on
end process;

p_rom_access: process (clk)  
begin 
    if (clk'event and clk = '1') then
        if (ce0 = '1') then 
            q0 <= mem(CONV_INTEGER(addr0_tmp)); 
        end if;
        if (ce1 = '1') then 
            q1 <= mem(CONV_INTEGER(addr1_tmp)); 
        end if;
    end if;
end process;

end rtl;

Library IEEE;
use IEEE.std_logic_1164.all;

entity mic16_tf_zone_streaming_hls_q2_24_k_zone_offsets_q2_24 is
    generic (
        DataWidth : INTEGER := 10;
        AddressRange : INTEGER := 25;
        AddressWidth : INTEGER := 5);
    port (
        reset : IN STD_LOGIC;
        clk : IN STD_LOGIC;
        address0 : IN STD_LOGIC_VECTOR(AddressWidth - 1 DOWNTO 0);
        ce0 : IN STD_LOGIC;
        q0 : OUT STD_LOGIC_VECTOR(DataWidth - 1 DOWNTO 0);
        address1 : IN STD_LOGIC_VECTOR(AddressWidth - 1 DOWNTO 0);
        ce1 : IN STD_LOGIC;
        q1 : OUT STD_LOGIC_VECTOR(DataWidth - 1 DOWNTO 0));
end entity;

architecture arch of mic16_tf_zone_streaming_hls_q2_24_k_zone_offsets_q2_24 is
    component mic16_tf_zone_streaming_hls_q2_24_k_zone_offsets_q2_24_rom is
        port (
            clk : IN STD_LOGIC;
            addr0 : IN STD_LOGIC_VECTOR;
            ce0 : IN STD_LOGIC;
            q0 : OUT STD_LOGIC_VECTOR;
            addr1 : IN STD_LOGIC_VECTOR;
            ce1 : IN STD_LOGIC;
            q1 : OUT STD_LOGIC_VECTOR);
    end component;



begin
    mic16_tf_zone_streaming_hls_q2_24_k_zone_offsets_q2_24_rom_U :  component mic16_tf_zone_streaming_hls_q2_24_k_zone_offsets_q2_24_rom
    port map (
        clk => clk,
        addr0 => address0,
        ce0 => ce0,
        q0 => q0,
        addr1 => address1,
        ce1 => ce1,
        q1 => q1);

end architecture;


