# 在唯一现有Vivado工程生成新的硬件候选，不连接板卡、不覆盖prebuilt。
set root [file normalize [file join [file dirname [info script]] ../..]]
if {[llength $argv]<1 || [llength $argv]>2 || ![regexp {^[a-z][a-z0-9_]{0,47}$} [lindex $argv 0]]} {error "请指定全新的候选输出名称"}
set irq_mode [expr {[llength $argv]==2 && [lindex $argv 1] eq "irq_capture"}]
if {[llength $argv]==2 && !$irq_mode} {error "第二个参数只能为irq_capture"}
set out [file join $root build hardware [lindex $argv 0]]
if {[file exists $out]} {error "候选目录已存在，必须保留旧结果"}
file mkdir $out
proc phase {name} {
 global out
 set f [open [file join $out progress.txt] w]
 puts $f "$name [clock format [clock seconds] -format {%Y-%m-%d %H:%M:%S}]"
 close $f
 puts "AV_HARDWARE_PHASE $name"
}
phase PREFLIGHT
open_project [file join $root fpga vivado mic16_acoustic_imaging.xpr]
if {[get_property PART [current_project]] ne "xc7z020clg484-2"} {error "器件不符合当前硬件合同"}
if {[llength [get_ips -quiet -filter {IS_LOCKED == 1}]]} {error "存在锁定IP，停止综合"}
report_ip_status -file [file join $out ip_status.rpt]
set_property STEPS.SYNTH_DESIGN.ARGS.FLATTEN_HIERARCHY rebuilt [get_runs synth_1]
set_property STEPS.OPT_DESIGN.TCL.PRE [file join $root fpga scripts apply_timing.tcl] [get_runs impl_1]
set f [open [file join $out source_files.txt] w]
foreach p [get_files -all] {puts $f [get_property NAME $p]}
close $f
if {$irq_mode} {
 # 只重建原工程运行目录；旧配套BIT/XSA及布线检查点必须已经保存。
 foreach name {ac880_acoustic_imaging.bit ac880_acoustic_imaging.xsa acoustic_imaging_routed.dcp candidate.json} {
  if {![file isfile [file join $root build hardware av_cooperative_20261020 $name]]} {error "旧基线不完整，停止重建：$name"}
 }
 open_bd_design [file join $root fpga bd bd_mic16.bd]
 if {[get_property CONFIG.C_GPIO_WIDTH [get_bd_cells axi_gpio_2]]!=4 || [llength [get_bd_cells -quiet audio_capture_status]]!=1} {error "缺少新的状态接线"}
 foreach pair {{pl_pdm_top_16_0/overflow_error audio_capture_status/In1} {pl_pdm_top_16_0/pdm_clk_running audio_capture_status/In2} {audio_capture_status/dout axi_gpio_2/gpio_io_i}} {
  if {[get_bd_nets -of_objects [get_bd_pins [lindex $pair 0]]] ne [get_bd_nets -of_objects [get_bd_pins [lindex $pair 1]]]} {error "状态接线不符：$pair"}
 }
 if {[get_bd_nets -of_objects [get_bd_ports GPIO_0_tri_i]] ne [get_bd_nets -of_objects [get_bd_pins audio_capture_status/In0]]} {error "按钮输入接线不符"}
 validate_bd_design
 generate_target all [get_files bd_mic16.bd]
 update_compile_order -fileset sources_1
 reset_runs synth_1
}
phase SYNTHESIS
launch_runs synth_1 -jobs 2
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "综合未完成，保留失败运行"}
open_run synth_1
report_utilization -hierarchical -hierarchical_depth 2 -file [file join $out synthesis_utilization.rpt]
close_design
phase IMPLEMENTATION
launch_runs impl_1 -to_step route_design -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "布局布线未完成，保留失败运行"}
open_run impl_1
phase TIMING_AUDIT
report_timing_summary -report_unconstrained -max_paths 20 -file [file join $out timing_summary.rpt]
report_timing -delay_type max -max_paths 20 -path_type full_clock_expanded -file [file join $out setup_paths.rpt]
report_timing -delay_type min -max_paths 20 -path_type full_clock_expanded -file [file join $out hold_paths.rpt]
report_drc -file [file join $out drc.rpt]
report_cdc -details -file [file join $out cdc.rpt]
report_clock_interaction -file [file join $out clock_interaction.rpt]
report_exceptions -summary -file [file join $out exceptions.rpt]
report_methodology -file [file join $out methodology.rpt]
report_utilization -file [file join $out utilization.rpt]
report_io -file [file join $out io.rpt]
report_bus_skew -warn_on_violation -file [file join $out bus_skew.rpt]
check_timing -verbose -file [file join $out check_timing.rpt]
write_checkpoint [file join $out acoustic_imaging_routed.dcp]
set setup_path [get_timing_paths -delay_type max -max_paths 1]
set hold_path [get_timing_paths -delay_type min -max_paths 1]
if {[llength $setup_path]!=1 || [llength $hold_path]!=1} {error "缺少建立或保持路径，不能判为通过"}
set setup [get_property SLACK $setup_path]
set hold [get_property SLACK $hold_path]
set errors [get_drc_violations -quiet -filter {SEVERITY == Error}]
set f [open [file join $out implementation_metrics.txt] w]
puts $f "SETUP_SLACK_NS=$setup"
puts $f "HOLD_SLACK_NS=$hold"
puts $f "DRC_ERRORS=[llength $errors]"
puts $f "PART=[get_property PART [current_project]]"
puts $f "STRATEGY=[get_property STRATEGY [get_runs impl_1]]"
puts $f "TIMING_HOOK=[get_property STEPS.OPT_DESIGN.TCL.PRE [get_runs impl_1]]"
close $f
if {$setup<0 || $hold<0 || [llength $errors]} {error "新候选未过时序/DRC：setup=$setup hold=$hold DRC=[llength $errors]；不生成下载文件"}
foreach name {camera_pclk_ext hdmi_idck} {
 if {[llength [get_clocks -quiet $name]]!=1} {error "缺少接口时钟预算 $name"}
}
phase BITSTREAM
# 放在当前实现运行的标准位置，write_hw_platform才能包含同一次实现的bit。
set generated_bit [file join [get_property DIRECTORY [get_runs impl_1]] bd_mic16_wrapper.bit]
write_bitstream -force $generated_bit
file copy $generated_bit [file join $out ac880_acoustic_imaging.bit]
phase XSA_EXPORT
write_hw_platform -fixed -include_bit -file [file join $out ac880_acoustic_imaging.xsa]
phase COMPLETED
puts "AV_MATCHED_HARDWARE_CANDIDATE_BUILT setup=$setup hold=$hold NO_BOARD_ACCESS"
close_project
exit
