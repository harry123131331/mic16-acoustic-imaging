# 当前PL复现输入

使用Vivado 2020.2恢复现有纯音频BD。build/rebuild_pl.tcl调用release_bd.tcl，保持原BD的IP参数、连线和地址。相对原导出脚本，仅将run_remote_bd_flow从1改为0，使工程在本地out/pl内创建。PDM RTL、引脚文件及opt_design前置时序脚本来自同一dataflow工程；未加入摄像头、HDMI或旧备用顶层。

## 已补齐的输入

| IP | VLNV | 随附内容 |
|---|---|---|
| 共享频谱 | mic16:acoustic:m16_sp_shared:2.0 | component.xml、RTL、驱动、ROM数据 |
| 共享计算 | mic16:acoustic:m16_shared_projection_engine:6.0 | component.xml、RTL、驱动、ROM数据 |
| 空间扫描 | xilinx.com:hls:mic16_tf_zone_streaming_hls_q2_24:1.0 | component.xml、RTL、驱动、ROM数据 |

三个当前IP位于fpga/ip/release/，是Vivado复现入口唯一使用的自定义IP目录。其余旧IP及HLS源码作为已有设计记录保留，不从旧xpr猜测复现路径。发布目录的原始/提交SHA见release_input_origins.json；为符合提交规则，文件名及文件引用统一为小写英文，模块名、端口和VLNV不变。初始化.dat文件随RTL保留，不能按普通缓存删除。

V6 HLS C/C++快照位于fpga/hls/release/shared_compute，来自shared_fir_compact_candidate；共享频谱2.0快照位于fpga/hls/release/shared_spectrum。扫描C/C++在fpga/hls原目录。HLS 2020.2的V6报告与solution设置附在hls_reports。Vivado从上述已导出RTL重建不需要重跑HLS；重新执行C到RTL时需先核对各原设置和数值回归，不能把源码快照等同于本轮完成HLS复验。

## 使用顺序

1. 解压到英文短路径，例如C:/mic16-acoustic-imaging，配置README列出的工具。
2. 运行python build/check_pl_inputs.py。此项只检查文件、VLNV和SHA，不启动AMD工具。
3. 人工运行vivado.bat -mode batch -source build/rebuild_pl.tcl -tclargs project，恢复同一数据流BD并生成IP输出。
4. 人工运行vivado.bat -mode batch -source build/rebuild_pl.tcl -tclargs implement，执行综合、实现及资源/时序/DRC报告；时序或DRC未通过即不导出BIT/XSA。
5. 新结果只写out/pl，不覆盖board/prebuilt，也不下载。更换XSA后需重新生成匹配BSP并完成固件数值及实板检查。

## 本轮验证范围

已核对当前三个IP组件的文件引用、ROM数据、RTL/约束输入、工具版本及输入SHA；结果见pl_input_validation.json。PS已有146个编译单元从源文件重建并链接的记录。原匹配BIT/XSA/ELF保持不变。

本轮没有执行Vivado展开、HLS综合、布局布线或板卡操作。build/utilization.rpt和timing_summary.rpt属于已有硬件，不代表新脚本已在陌生机器上冷构建通过。尚缺整套PL冷构建的实际运行结果；必须保留此边界，不能以静态输入检查代替。
