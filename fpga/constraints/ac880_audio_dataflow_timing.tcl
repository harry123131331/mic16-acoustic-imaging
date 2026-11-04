# 纯音频数据流候选的时序边界；不放宽PDM两级同步器内部时序。
create_generated_clock -name bd_mic16_dataflow_i/clk_wiz_0/inst/clk_in1 -source [get_pins {bd_mic16_dataflow_i/processing_system7_0/inst/PS7_i/FCLKCLK[0]}] -divide_by 1 [get_pins bd_mic16_dataflow_i/clk_wiz_0/inst/clk_in1]
# 现有两级触发器链对PDM过采样，不将PDM另设为新时钟。
# 只切输入引脚到第一级亚稳触发器，不切第一级到第二级。
for {set i 0} {$i<16} {incr i} {
 set legacy_pat [format {.*GEN_DECIMATOR\[%d\].*/pdm_meta_reg/D$} $i]
 set shared_pat [format {.*GEN_SHARED[./]u_shared_decimator/pdm_meta_reg\[%d\]/D$} $i]
 set hit {}
 foreach p [get_pins -hier -filter {NAME =~ *pdm_meta_reg* && REF_PIN_NAME == D}] {
  if {[regexp $legacy_pat $p] || [regexp $shared_pat $p]} {lappend hit $p}
 }
 if {[llength $hit]!=1} {error "PDM第一级同步触发器不唯一：通道$i"}
 set_false_path -from [get_ports pdm_data${i}_0] -to $hit
 set c [get_cells -of_objects $hit]
 set_property ASYNC_REG TRUE $c
 set sync_name [string map {pdm_meta_reg pdm_sync_reg} $c]
 set sync {}
 foreach candidate [get_cells -hier -filter {NAME =~ *pdm_sync_reg*}] {
  if {$candidate eq $sync_name} {lappend sync $candidate}
 }
 if {[llength $sync]!=1} {error "PDM第二级同步触发器不唯一：通道$i"}
 set_property ASYNC_REG TRUE $sync
}
# 按键为异步输入；LED和PDM时钟外部接收预算须实板另验。
set_false_path -from [get_ports {GPIO_0_tri_i[*]}]
set_false_path -to [get_ports {GPIO2_0_tri_o[*] pdm_clk_out_0[*]}]
