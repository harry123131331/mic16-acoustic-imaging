# 仅比较实/虚分时复用版本；当前基线IP不变。
set root [file normalize [file join [file dirname [info script]] ../../..]]
set src [file join $root fpga hls statistics_tile]
file mkdir [file join $root build hls statistics_tile_serial_20261023]
cd [file join $root build hls statistics_tile_serial_20261023]
open_project phase_tile
set_top m16_statistics_tile
add_files [file join $src src statistics_tile.cpp] -cflags "-I$src/src -ffp-contract=off -fno-fast-math -DM16_TILE_SERIAL_COMPONENTS=1"
add_files -tb [file join $src test statistics_tile_test.cpp] -cflags "-I$src/src -ffp-contract=off -fno-fast-math -DM16_TILE_SERIAL_COMPONENTS=1"
open_solution one_component
set_part {xc7z020clg484-2}
create_clock -period 10 -name default
config_op fadd -impl fabric
config_op fsub -impl fabric
csim_design
csynth_design
exit
