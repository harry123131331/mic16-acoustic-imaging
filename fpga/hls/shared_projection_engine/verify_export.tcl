# 三模式共享引擎联仿及导出；正式BD仍为已布线的版本2，不下载板卡。
set root [file normalize [file join [file dirname [info script]] ../../..]]
proc verify_export {root} {
    set out [file normalize [file join $root build hls shared_projection_physical_20261024]]
    cd $out
    open_project shared_projection
    open_solution shared_modes
    cosim_design -rtl verilog -trace_level none
    puts "M16_SHARED_PROJECTION_RTL_COSIM_FINISHED"
    export_design -format ip_catalog -rtl verilog -vendor mic16 -library acoustic -version 3.0
}
if {[catch {verify_export $root} message]} {
    puts stderr "M16_SHARED_ENGINE_VERIFY_EXPORT_FAILED: $message"
    exit 1
}
puts "M16_SHARED_PROJECTION_IP_EXPORTED_NOT_BOARD_OR_FULL_ARCHITECTURE"
exit 0
