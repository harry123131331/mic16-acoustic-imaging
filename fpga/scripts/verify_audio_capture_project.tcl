# 为同一个源工程生成依赖并核验，不启动综合或布局布线，不写BIT/XSA。
set root [file normalize [file join [file dirname [info script]] ../..]]
set out [file join $root build verification audio_capture_project_20261021]
file mkdir $out
open_project [file join $root fpga vivado mic16_acoustic_imaging.xpr]
open_bd_design [file join $root fpga bd bd_mic16.bd]
validate_bd_design
generate_target all [get_files bd_mic16.bd]
update_compile_order -fileset sources_1
set wrappers [make_wrapper -files [get_files bd_mic16.bd] -top]
set fp [open [file join $out dependencies.txt] w]
set count 0
foreach f [get_files -all] {
 set path [get_property NAME $f]
 if {![file exists $path]} {close $fp;error "依赖文件缺失：$path"}
 puts $fp $path
 incr count
}
close $fp
set fp [open [file join $out wrapper_paths.txt] w]
foreach f $wrappers {puts $fp $f}
close $fp
puts "AUDIO_STATUS_PROJECT_DEPENDENCIES_PASS files=$count NO_SYNTHESIS_NO_BIT_NO_BOARD"
close_project
