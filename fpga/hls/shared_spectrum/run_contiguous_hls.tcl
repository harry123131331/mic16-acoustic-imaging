# 同一提交工程的内部构建变体；不改Vivado/Vitis正式工程或IP仓库。
set root [file normalize [file join [file dirname [info script]] ../../..]]
set src [file join $root fpga hls shared_spectrum]
cd [file join $root build hls shared_spectrum_contiguous_20261023]
open_project shared_fft
set_top m16_sp_shared
add_files [file join $src src shared_spectrum.cpp] -cflags "-I$src/src -I$src/reference"
add_files [file join $src reference mic16_fft_engine_hls.cpp] -cflags "-I$src/reference"
open_solution s
set_part {xc7z020clg484-2}
create_clock -period 8 -name default
config_dataflow -default_channel fifo -fifo_depth 4
config_op fadd -impl fabric
config_op fsub -impl fabric
csynth_design
exit
