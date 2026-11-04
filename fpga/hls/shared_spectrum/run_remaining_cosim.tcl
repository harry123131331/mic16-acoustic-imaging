# 同一已综合RTL补测候选到旧模式的切换，不更改HLS硬件实现。
set root [file normalize [file join [file dirname [info script]] ../../..]]
set src [file join $root fpga hls shared_spectrum]
cd [file join $root build hls shared_spectrum_20261023]
open_project shared_fft
set_top m16_sp_shared
open_solution s
csim_design -clean
cosim_design -rtl verilog -tool xsim -trace_level none
exit
