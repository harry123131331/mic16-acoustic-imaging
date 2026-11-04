# 只读核查布线检查点中的PDM同步链；不修改约束、不消除CDC告警、不连接板卡。
set root [file normalize [file join [file dirname [info script]] ../..]]
if {[llength $argv]!=1 || ![regexp {^[a-z][a-z0-9_]{0,47}$} [lindex $argv 0]]} {error "请指定候选名"}
set hardware [file join $root build hardware [lindex $argv 0]]
set out [file join $root build verification [lindex $argv 0]_structure]
if {[file exists $out]} {error "核验目录已存在"}
file mkdir $out
open_checkpoint [file join $hardware acoustic_imaging_routed.dcp]
set f [open [file join $out pdm_synchronizers.txt] w]
for {set i 0} {$i<16} {incr i} {
 set pattern [format {^.*/GEN_DECIMATOR\[%d\]\.u_decim/pdm_meta_reg$} $i]
 set a [get_cells -hier -regexp $pattern]
 if {[llength $a]!=1} {error "PDM第一级不是唯一：$i"}
 set b [get_cells -quiet [string map {pdm_meta_reg pdm_sync_reg} $a]]
 if {[llength $b]!=1 || ![get_property ASYNC_REG $a] || ![get_property ASYNC_REG $b]} {error "PDM同步属性缺失：$i"}
 set fanout [all_fanout -flat -endpoints_only -from [get_pins $a/Q]]
 if {[llength $fanout]!=1 || $fanout ne "$b/D"} {error "PDM亚稳级存在额外扇出：$i $fanout"}
 set ca [get_clocks -of_objects [get_pins $a/C]]
 set cb [get_clocks -of_objects [get_pins $b/C]]
 if {[llength $ca]!=1 || $ca ne $cb} {error "PDM同步链时钟不符：$i"}
 puts $f "CHANNEL=$i FIRST=$a SECOND=$b ASYNC_REG=TRUE FANOUT=ONLY_SECOND_D CLOCK=$ca"
}
close $f
report_control_sets -verbose -file [file join $out control_sets.rpt]
report_bus_skew -warn_on_violation -file [file join $out bus_skew.rpt]
# 对比所有外部位的封装脚、方向和电平标准，不能只看端口声明。
proc pins_to_file {path} {
 set f [open $path w]
 foreach name [lsort [get_ports *]] {
  set p [get_ports $name]
  puts $f "$name\t[get_property DIRECTION $p]\t[get_property PACKAGE_PIN $p]\t[get_property IOSTANDARD $p]"
 }
 close $f
}
pins_to_file [file join $out top_pins.tsv]
close_design
open_checkpoint [file join $root build hardware av_cooperative_20261020 acoustic_imaging_routed.dcp]
pins_to_file [file join $out baseline_top_pins.tsv]
set a [open [file join $out top_pins.tsv] r];set av [read $a];close $a
set b [open [file join $out baseline_top_pins.tsv] r];set bv [read $b];close $b
if {$av ne $bv} {error "新旧封装引脚、电平或方向改变"}
puts "AV_ROUTED_PDM_CHAIN_AND_EXTERNAL_PINS_AUDIT_PASS_NO_CDC_WAIVERS_ADDED"
close_design
exit
