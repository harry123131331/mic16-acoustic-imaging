# 提交包的离线PL复现入口；不连接板卡，不替换预编译BIT/XSA。
# project恢复同一数据流BD；implement显式执行综合与布局布线。
set root [file normalize [file join [file dirname [info script]] ..]]
set mode project
if {[llength $argv]} {set mode [lindex $argv 0]}
if {$mode ni {project implement}} {error "仅支持project或implement参数"}
if {[version -short] ne "2020.2"} {error "请使用Vivado 2020.2；其他版本尚未核验"}
if {[regexp {[^\x00-\x7f]} $root]} {error "请先将提交包解压到英文短路径，例如C:/mic16-acoustic-imaging"}
set out [file join $root out pl]
set xpr [file join $out project mic16_acoustic_imaging.xpr]
set repos {}
foreach name {shared_spectrum shared_compute scanner} {
    set repo [file join $root fpga ip release $name]
    if {![file exists [file join $repo component.xml]]} {error "缺少发布IP目录：$name"}
    lappend repos $repo
}
if {$mode eq "project"} {
    if {[file exists $xpr]} {error "复现工程已存在；请使用新解压目录，避免覆盖"}
    file mkdir $out
    create_project mic16_acoustic_imaging [file join $out project] -part xc7z020clg484-2
    set_property target_language Verilog [current_project]
    set_property ip_repo_paths $repos [current_project]
    update_ip_catalog
    # 与原dataflow_sources保持一致，不加入旧摄像头或备用顶层。
    foreach name {mic16_cic_compensator pdm_axis_pack_nch pdm_clk_gen pdm_decimator pdm_decimator_shared pl_pdm_top_16} {
        add_files -norecurse [file join $root fpga rtl audio ${name}.v]
    }
    source [file join $root build release_bd.tcl]
    set bd [get_files -quiet */bd_mic16_dataflow.bd]
    if {[llength $bd]!=1} {error "数据流BD不唯一"}
    generate_target all $bd
    add_files -norecurse [make_wrapper -files $bd -top]
    set_property top bd_mic16_dataflow_wrapper [current_fileset]
    add_files -fileset constrs_1 -norecurse [file join $root fpga constraints ac880_audio_dataflow_pins.xdc]
    set timing [file join $root fpga constraints ac880_audio_dataflow_timing.tcl]
    add_files -fileset utils_1 -norecurse $timing
    # 沿用原实现的opt_design前置时序脚本及默认实现策略。
    set_property STEPS.OPT_DESIGN.TCL.PRE $timing [get_runs impl_1]
    update_compile_order -fileset sources_1
    close_project
    puts "PL复现工程已生成，尚未综合或实现：$xpr"
    exit 0
}
if {![file exists $xpr]} {error "请先使用project参数恢复工程"}
open_project $xpr
set_property ip_repo_paths $repos [current_project]
update_ip_catalog
launch_runs synth_1 -jobs 2
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "综合未完成"}
launch_runs impl_1 -to_step route_design -jobs 2
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "布局布线未完成"}
open_run impl_1
report_utilization -file [file join $out utilization.rpt]
report_timing_summary -report_unconstrained -max_paths 20 -file [file join $out timing_summary.rpt]
report_drc -file [file join $out drc.rpt]
report_cdc -details -file [file join $out cdc.rpt]
check_timing -verbose -file [file join $out check_timing.rpt]
set setup [get_property SLACK [get_timing_paths -delay_type max -max_paths 1]]
set hold [get_property SLACK [get_timing_paths -delay_type min -max_paths 1]]
set errors [llength [get_drc_violations -quiet -filter {SEVERITY == Error}]]
if {$setup<0 || $hold<0 || $errors} {error "重建结果未通过时序或DRC，不导出运行包"}
write_bitstream -force [file join $out ac880_audio_dataflow.bit]
write_hw_platform -fixed -include_bit -file [file join $out ac880_audio_dataflow.xsa]
close_project
puts "PL重建完成；结果仅在out/pl中，未下载板卡。"
exit 0
