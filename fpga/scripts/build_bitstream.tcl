# 从提交源码重新综合、布局布线；不连接板卡。
# 本轮只验证重建与BD，不声称此脚本已经完成一次新的时序收敛。
set repo [file normalize [file join [file dirname [info script]] ../..]]
open_project [file join $repo fpga vivado mic16_acoustic_imaging.xpr]
set_property STEPS.SYNTH_DESIGN.ARGS.FLATTEN_HIERARCHY rebuilt [get_runs synth_1]
set_property STEPS.OPT_DESIGN.TCL.PRE [file join $repo fpga scripts apply_timing.tcl] [get_runs impl_1]
launch_runs synth_1 -jobs 2
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "综合没有完成，请查看运行日志"}
launch_runs impl_1 -to_step route_design -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "布局布线没有完成，请查看运行日志"}
open_run impl_1
set out [file join $repo build implementation]
file mkdir $out
report_timing_summary -report_unconstrained -max_paths 10 -file [file join $out timing_summary.rpt]
report_drc -file [file join $out drc.rpt]
report_cdc -details -file [file join $out cdc.rpt]
report_utilization -file [file join $out utilization.rpt]
check_timing -verbose -file [file join $out check_timing.rpt]
set setup [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set hold [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
if {$setup < 0 || $hold < 0} {error "时序未通过：建立裕量=$setup，保持裕量=$hold；不生成下载文件"}
if {[llength [get_drc_violations -quiet -filter {SEVERITY == Error}]]} {error "存在DRC错误，不生成下载文件"}
write_checkpoint -force [file join $out acoustic_imaging_routed.dcp]
write_bitstream -force [file join $out ac880_acoustic_imaging.bit]
# 仅在明确完成时序与DRC检查后生成候选；仍需实板接口验收。
puts "ACOUSTIC_BITSTREAM_BUILT setup=$setup hold=$hold"
close_project
