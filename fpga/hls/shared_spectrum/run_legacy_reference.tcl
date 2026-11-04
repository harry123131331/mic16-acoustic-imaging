# 同一工程、同一器件和时钟；旧前端只作为资源对照，不替换新top或正式BD。
set root [file normalize [file join [file dirname [info script]] ../../..]]
set src [file join $root fpga hls shared_spectrum]
cd [file join $root build hls shared_spectrum_20261023]
open_project shared_fft
set_top m16_tf_front_q24
add_files [file join $src reference mic16_tf_phat_frontend_hls_q2_24.cpp] -cflags "-I$src/reference"
open_solution legacy_reference
set_part {xc7z020clg484-2}
create_clock -period 8 -name default
config_dataflow -default_channel fifo -fifo_depth 4
config_op fadd -impl fabric
config_op fsub -impl fabric
csynth_design
close_solution
set_top m16_sp_shared
close_project
exit
