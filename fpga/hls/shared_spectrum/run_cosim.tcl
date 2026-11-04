# 沿用已经综合的同一工程，不重新创建Vivado/Vitis工作区。
set root [file normalize [file join [file dirname [info script]] ../../..]]
set src [file join $root fpga hls shared_spectrum]
cd [file join $root build hls shared_spectrum_20261023]
open_project shared_fft
set_top m16_sp_shared
open_solution s
add_files -tb [file join $src test shared_spectrum_rtl.cpp] -cflags "-I$src/src -I$src/reference"
csim_design -clean
cosim_design -rtl verilog -tool xsim -trace_level none
exit
