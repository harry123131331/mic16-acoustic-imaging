# 保留按钮低两位及原顶层引脚，扩宽GPIO输入读取音频状态。
# 默认只在内存验证；显式apply才保存，不创建第二工程，不连接板卡。
set root [file normalize [file join [file dirname [info script]] ../..]]
set apply [expr {[llength $argv]==1 && [lindex $argv 0] eq "apply"}]
if {[llength $argv]>1 || ([llength $argv]==1 && !$apply)} {error "参数只允许省略或apply"}
set project [file join $root fpga vivado mic16_acoustic_imaging.xpr]
set bd [file join $root fpga bd bd_mic16.bd]
open_project $project
open_bd_design $bd
if {[get_property CONFIG.FIFO_DEPTH [get_bd_cells axis_data_fifo_0]]!=1024} {error "AXIS FIFO深度已改变"}
if {![llength [get_bd_cells -quiet audio_capture_status]]} {
 if {[get_property CONFIG.C_GPIO_WIDTH [get_bd_cells axi_gpio_2]]!=2} {error "原按钮GPIO宽度不符"}
 set ext [get_bd_intf_ports -quiet GPIO_0]
 set pin [get_bd_intf_pins axi_gpio_2/GPIO]
 set net [get_bd_intf_nets -quiet -of_objects $ext]
 if {[llength $ext]!=1 || [llength $net]!=1 || $net ne [get_bd_intf_nets -of_objects $pin]} {error "原按钮接口连接不符，禁止覆盖"}
 # 展开原二位接口后，包装器/XDC中的GPIO_0_tri_i名字及位宽不变。
 delete_bd_objs $net
 delete_bd_objs $ext
 set_property CONFIG.C_GPIO_WIDTH 4 [get_bd_cells axi_gpio_2]
 create_bd_port -dir I -from 1 -to 0 GPIO_0_tri_i
 create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 audio_capture_status
 set_property -dict [list CONFIG.NUM_PORTS {3} CONFIG.IN0_WIDTH {2} CONFIG.IN1_WIDTH {1} CONFIG.IN2_WIDTH {1}] [get_bd_cells audio_capture_status]
 connect_bd_net [get_bd_ports GPIO_0_tri_i] [get_bd_pins audio_capture_status/In0]
 connect_bd_net [get_bd_pins pl_pdm_top_16_0/overflow_error] [get_bd_pins audio_capture_status/In1]
 connect_bd_net [get_bd_pins pl_pdm_top_16_0/pdm_clk_running] [get_bd_pins audio_capture_status/In2]
 connect_bd_net [get_bd_pins audio_capture_status/dout] [get_bd_pins axi_gpio_2/gpio_io_i]
}
if {[get_property CONFIG.C_GPIO_WIDTH [get_bd_cells axi_gpio_2]]!=4} {error "状态GPIO宽度不符"}
foreach pair {{pl_pdm_top_16_0/overflow_error audio_capture_status/In1} {pl_pdm_top_16_0/pdm_clk_running audio_capture_status/In2} {audio_capture_status/dout axi_gpio_2/gpio_io_i}} {
 set a [get_bd_nets -of_objects [get_bd_pins [lindex $pair 0]]]
 set b [get_bd_nets -of_objects [get_bd_pins [lindex $pair 1]]]
 if {[llength $a]!=1 || $a ne $b} {error "状态接线核验失败：$pair"}
}
if {[get_bd_nets -of_objects [get_bd_ports GPIO_0_tri_i]] ne [get_bd_nets -of_objects [get_bd_pins audio_capture_status/In0]]} {error "按钮低位连接丢失"}
validate_bd_design
if {$apply} {
 save_bd_design
 update_compile_order -fileset sources_1
 puts "AUDIO_CAPTURE_STATUS_SAVED_BUTTONS_PRESERVED_NEEDS_NEW_BIT_XSA_NO_BOARD_ACCESS"
} else {
 puts "AUDIO_CAPTURE_STATUS_VALIDATED_BUTTONS_PRESERVED_NOT_SAVED_NO_BOARD_ACCESS"
}
close_project
