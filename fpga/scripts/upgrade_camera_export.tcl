# 在唯一现有工程升级相机IP，工具更新封装与生成文件，不手工修改XCI。
set root [file normalize [file join [file dirname [info script]] ../..]]
set out [file join $root build verification video_export_ip_20261020]
file mkdir $out
open_project [file join $root fpga vivado mic16_acoustic_imaging.xpr]
ipx::open_core [file join $root fpga ip camera component.xml]
set core [ipx::current_core]
set_property version 1.2 $core
foreach group [ipx::get_file_groups -of_objects $core] {
 set name [get_property NAME $group]
 if {[string match *xilinx_anylanguagesynthesis* $name] || [string match *xilinx_anylanguagebehavioralsimulation* $name]} {
  foreach file {src/mic16_frame_banks_export.sv src/mic16_video_export_regs.sv} {
   if {[llength [ipx::get_files $file -of_objects $group]]==0} {
    set f [ipx::add_file $file $group]
    set_property type systemVerilogSource $f
   }
  }
 }
}
ipx::update_checksums $core
ipx::save_core $core
update_ip_catalog -rebuild
open_bd_design [file join $root fpga bd bd_mic16.bd]
set camera [get_bd_cells -hier -filter {VLNV =~ "mic16.local:av:mic16_camera_path:*"}]
if {[llength $camera]!=1} {error "相机实例不是唯一，停止升级"}
report_ip_status -file [file join $out ip_status_before.txt]
# 同版本源码/封装更新也需刷新实例，不能只比较版本号。
upgrade_bd_cells $camera
# 升级后旧Tcl对象失效，必须重新查询，不能在旧句柄上读取属性。
set camera [get_bd_cells -hier -filter {VLNV == "mic16.local:av:mic16_camera_path:1.2"}]
if {[llength $camera]!=1} {error "相机IP未升级到1.2"}
validate_bd_design
save_bd_design
generate_target all [get_files bd_mic16.bd]
update_compile_order -fileset sources_1
set fp [open [file join $out sources.txt] w]
foreach f [get_files -all] {puts $fp [get_property NAME $f]}
close $fp
report_ip_status -file [file join $out ip_status.txt]
puts "VIDEO_EXPORT_CAMERA_IP_1_2_VALIDATED_NO_BIT_OR_BOARD"
close_project
