# 新增模式4的最大批量PS格式和AXI读取检查；无板卡操作。
set root [file normalize [file join [file dirname [info script]] ../../..]]
set src [file join $root fpga hls shared_spectrum]
set ps [file join $root firmware acceleration shared_spectrum]
cd [file join $root build hls shared_spectrum_contiguous_20261023]
open_project shared_fft
set_top m16_sp_shared
open_solution s
add_files -tb [file join $src test shared_spectrum_contiguous_rtl.cpp] -cflags "-I$src/src -I$src/reference -I$ps/include"
add_files -tb [file join $ps src mic16_shared_spectrum_pack.c] -cflags "-I$ps/include"
csim_design -clean
cosim_design -rtl verilog -tool xsim -trace_level none
exit
