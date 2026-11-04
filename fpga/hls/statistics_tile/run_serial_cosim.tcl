# 验证最大块、尾块、异常输入及跨调用状态；不是实板计时。
set root [file normalize [file join [file dirname [info script]] ../../..]]
set src [file join $root fpga hls statistics_tile]
cd [file join $root build hls statistics_tile_serial_20261023]
open_project phase_tile
set_top m16_statistics_tile
open_solution one_component
remove_files [file join $src test statistics_tile_test.cpp]
add_files -tb [file join $src test statistics_tile_test.cpp] -cflags "-I$src/src -ffp-contract=off -fno-fast-math -DM16_TILE_SERIAL_COMPONENTS=1 -DM16_TILE_RTL_SMOKE=1"
csim_design -clean
cosim_design -rtl verilog -tool xsim -trace_level none
exit
