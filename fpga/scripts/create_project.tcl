# 使用Vivado 2020.2重建离线工程，不连接板卡。
set repo [file normalize [file join [file dirname [info script]] ../..]]
set target [file join $repo fpga vivado]
if {[file exists [file join $target mic16_acoustic_imaging.xpr]]} {error "工程已存在，请直接打开；本脚本不覆盖现有工程。"}
create_project mic16_acoustic_imaging $target -part xc7z020clg484-2
set_property target_language Verilog [current_project]
set_property simulator_language Mixed [current_project]
set_property XPM_LIBRARIES {XPM_CDC XPM_FIFO XPM_MEMORY} [current_project]
set repos {}
foreach name {frontend scanner wdas camera} {lappend repos [file join $repo fpga ip $name]}
set_property ip_repo_paths $repos [current_project]
update_ip_catalog
add_files -norecurse [glob [file join $repo fpga rtl audio *.v]]
add_files -norecurse [file join $repo fpga rtl bd_mic16_wrapper.v]
add_files -norecurse [file join $repo fpga bd bd_mic16.bd]
add_files -fileset constrs_1 -norecurse [file join $repo fpga constraints ac880_acoustic_imaging_pins.xdc]
set_property top bd_mic16_wrapper [get_filesets sources_1]
set_property STEPS.OPT_DESIGN.TCL.PRE [file join $repo fpga scripts apply_timing.tcl] [get_runs impl_1]
open_bd_design [file join $repo fpga bd bd_mic16.bd]
validate_bd_design
save_bd_design
update_compile_order -fileset sources_1
puts "ACOUSTIC_IMAGING_PROJECT_CREATED_NO_BOARD_ACCESS"
close_project
