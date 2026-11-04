# 新数据流BD接口预算：支持并行基线或共享PDM通路，不是实测板级IO时序。
# 传感器PCLK与数据偏斜、PCB偏斜仍须实板验收。
# 按OV5640数据手册表8-5的并行PCLK上限84MHz建立预算。
# 不整组切断异步时钟；保留XPM/FIFO最大延迟和总线偏斜约束。
# 将IP输入主时钟替换为与上游PS实际相关的生成时钟。
create_generated_clock -name bd_mic16_i/clk_wiz_0/inst/clk_in1 -source [get_pins {bd_mic16_i/processing_system7_0/inst/PS7_i/FCLKCLK[0]}] -divide_by 1 [get_pins bd_mic16_i/clk_wiz_0/inst/clk_in1]
create_clock -name camera_pclk_ext -period 11.904762 [get_ports camera_pclk]
set_input_jitter [get_clocks camera_pclk_ext] 0.200
# 工程接口合同：数据在PCLK下降沿发出，假设到达范围为-1～4ns。
# 这两个值包含假设的模组/线缆预算，不是数据手册保证值。
set_input_delay -clock camera_pclk_ext -clock_fall -min -1.000 [get_ports {camera_data[*] camera_href camera_vsync}]
set_input_delay -clock camera_pclk_ext -clock_fall -max 4.000 [get_ports {camera_data[*] camera_href camera_vsync}]
# 原厂ROM的TPI地址0x08写0x60，选择下降沿采样，参考Linux sii902x的BIT(4)。
# 当前800×480显示模块转发非反相显示时钟。
create_generated_clock -name hdmi_idck -source [get_pins bd_mic16_i/camera/inst/pll/inst/plle2_adv_inst/CLKOUT1] -divide_by 1 [get_ports TFT_clk]
# SiI-DS-1076-E.01表3.13规定下降沿IDCK的建立0、保持1.8ns。
# 额外假设板级时钟/数据偏斜±0.5ns，必须核对实际板卡和模组。
set_output_delay -clock hdmi_idck -clock_fall -max 0.500 [get_ports {TFT_rgb[*] TFT_de TFT_hs TFT_vs}]
set_output_delay -clock hdmi_idck -clock_fall -min -2.300 [get_ports {TFT_rgb[*] TFT_de TFT_hs TFT_vs}]
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
# 按键为异步输入；LED、静态时钟、PWM及慢速控制端口
# 在本预算中没有逐周期接收合同，因此显式列出例外，
# 不能虚构零输入/输出延迟使check_timing表面完整。
set_false_path -from [get_ports {GPIO_0_tri_i[*]}]
set_false_path -to [get_ports {GPIO2_0_tri_o[*] pdm_clk_out_0[*] TFT_pwm camera_xclk camera_sclk SiI9022_sclk}]
# 仅约束引脚到第一级，不能切断第一级Q到第二级D。
set sda_meta_cells [get_cells -hier -filter {NAME =~ *sda_meta_reg}]
if {[llength $sda_meta_cells]!=2} {error {Exactly two SDA first-stage flops required}}
foreach meta $sda_meta_cells {
 set sync [get_cells [string map {sda_meta_reg sda_sync_reg} $meta]]
 if {[llength $sync]!=1 || ![get_property ASYNC_REG $meta] || ![get_property ASYNC_REG $sync]} {error {SDA ASYNC_REG chain missing}}
 set first [get_pins $meta/D]
 set pads [filter [all_fanin -flat -startpoints_only -to $first] {CLASS == port}]
 if {[llength $pads]!=1 || $pads ni {camera_sdat SiI9022_sdat}} {error "SDA wrong pad: $pads"}
 set loads [all_fanout -flat -endpoints_only -from [get_pins $meta/Q]]
 if {[llength $loads]!=1 || $loads ne "$sync/D"} {error "SDA first stage fanout: $loads"}
 set_false_path -from $pads -to $first
 puts "REV7_SDA_CHAIN $pads $meta $sync"
}
set_false_path -to [get_ports {camera_sdat SiI9022_sdat}]
# 自定义单比特FIFO复位忙同步器只限制异步第一级，
# 第二级仍正常检查建立/保持时间。
set syncs [get_pins -hier -regexp {^.*/frame_store/(rf_rbusy_sync_reg|wf_wbusy_sync_reg)\[0\]/D$}]
if {[llength $syncs]!=2} {error "Two busy synchronizer first stages required"}
foreach p $syncs {
 set starts [all_fanin -flat -startpoints_only -to $p]
 set_max_delay -datapath_only 10.000 -from $starts -to $p
}
# 叠加返回翻转同步器只对异步第一级设置边界。
set ackroot bd_mic16_i/camera/inst/overlay/overlay
set ackfirst [get_cells $ackroot/ack_meta_reg]
set acksecond [get_cells $ackroot/ack_sync_reg]
if {![get_property ASYNC_REG $ackfirst] || ![get_property ASYNC_REG $acksecond]} {error "Overlay ack two-FF attributes missing"}
set ackloads [all_fanout -flat -endpoints_only -from [get_pins $ackroot/ack_meta_reg/Q]]
if {[llength $ackloads]!=1 || $ackloads ne "$ackroot/ack_sync_reg/D"} {error "Overlay first-stage ack fanout changed: $ackloads"}
set_max_delay -datapath_only 10.000 -from [get_cells $ackroot/destination_ack_reg] -to [get_pins $ackroot/ack_meta_reg/D]
# 复位释放链两级CLR都故意异步，
# 第0级Q到第1级D及下游局部复位仍保留时序分析。
set resetpins [get_pins -hier -regexp {^.*/overlay/overlay/(axi_reset_pipe_reg|pix_reset_pipe_reg)\[[01]\]/CLR$}]
if {[llength $resetpins]!=4} {error "Expected exactly four custom reset CLR pins"}
set_false_path -to $resetpins
