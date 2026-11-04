set ROOT [file normalize [file join [file dirname [info script]] ..]]
# 仅CPU0启动裸机；先停止双核，避免原厂Linux恢复后占用DDR。
proc checked {script} {
    if {[catch {uplevel 1 $script} message]} {puts stderr "BOARD_STEP_FAILED $message";catch {disconnect};exit 2}
}
checked {connect -url tcp:127.0.0.1:3121}
foreach core {0 1} {
    set found [targets -target-properties -filter [format {name =~ "*Cortex-A9*#%d"} $core]]
    if {[llength $found]!=1} {error "必须唯一识别一块板卡的两个ARM核，停止下载"}
}
checked {targets -set -filter {name =~ "*Cortex-A9*#0"}}
checked {loadhw -hw [file join $ROOT board prebuilt {ac880_audio_dataflow.xsa}] -mem-ranges [list [list 0x00100000 0x3fffffff]]}
checked {targets -set -filter {name == "APU"}}
checked {rst -system -stop}
after 1000
foreach core {1 0} {
    checked [list targets -set -filter [format {name =~ "*Cortex-A9*#%d"} $core]]
    set state [lindex [targets -target-properties -filter [format {name =~ "*Cortex-A9*#%d"} $core]] 0]
    if {![dict get $state suspended]} {checked {stop}}
}
checked {targets -set -filter {name =~ "*Cortex-A9*#0"}}
checked {fpga -file [file join $ROOT board prebuilt {ac880_audio_dataflow.bit}]}
checked {source [file join $ROOT board prebuilt {ps7_init.tcl}]}
checked {ps7_init}
checked {ps7_post_config}
checked {rst -processor -clear-registers}
checked {dow [file join $ROOT board prebuilt {hemisphere_localization.elf}]}
# 初始化和下载后再次停止CPU1，避免原启动代码抢用DDR。
checked {targets -set -filter {name =~ "*Cortex-A9*#1"}}
if {[catch {stop} message] && $message ne "Already stopped"} {puts stderr $message;exit 2}
checked {targets -set -filter {name =~ "*Cortex-A9*#0"}}
checked {con}
puts BOARD_CPU0_STARTED_NOT_ACCEPTANCE
disconnect
exit
