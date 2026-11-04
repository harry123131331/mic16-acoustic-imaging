# 验证搬移后的源码依赖和BD，不复位、不下载，也不覆盖已有BIT。
set repo [file normalize [file join [file dirname [info script]] ../..]]
set xpr [file join $repo fpga vivado mic16_acoustic_imaging.xpr]
open_project $xpr
set_property STEPS.OPT_DESIGN.TCL.PRE [file join $repo fpga scripts apply_timing.tcl] [get_runs impl_1]
set repos {}
foreach name {frontend scanner wdas camera} {lappend repos [file join $repo fpga ip $name]}
set_property ip_repo_paths $repos [current_project]
update_ip_catalog
open_bd_design [file join $repo fpga bd bd_mic16.bd]
validate_bd_design
save_bd_design
generate_target all [get_files bd_mic16.bd]
update_compile_order -fileset sources_1
set count 0
foreach f [get_files -all] {
    set path [get_property NAME $f]
    if {![file exists $path]} {error "工程依赖文件不存在：$path"}
    incr count
}
file mkdir [file join $repo build verification hardware]
set fp [open [file join $repo build verification hardware dependencies.txt] w]
foreach f [get_files -all] {puts $fp [get_property NAME $f]}
close $fp
puts "ACOUSTIC_PROJECT_VALIDATED files=$count"
close_project
