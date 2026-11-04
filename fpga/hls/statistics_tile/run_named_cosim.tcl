# 最大块、尾块、符号零、非正规数、异常输入和跨调用状态检查。
set root [file normalize [file join [file dirname [info script]] ../../..]]
set src [file join $root fpga hls statistics_tile]
cd [file join $root build hls statistics_tile_named_20261023]
open_project phase_tile
set_top m16_statistics_tile
open_solution one_component_proven
add_files -tb [file join $src test statistics_tile_test.cpp] -cflags "-I$src/src -ffp-contract=off -fno-fast-math -DM16_TILE_SERIAL_COMPONENTS=1 -DM16_TILE_RTL_SMOKE=1"
csim_design -clean
cosim_design -rtl verilog -tool xsim -trace_level none
exit
