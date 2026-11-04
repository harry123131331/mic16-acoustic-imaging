# MIC16 Acoustic Imaging

16路麦克风持续声源定位与Qt声像显示。板卡为AC880，器件为xc7z020clg484-2；USB摄像头直接接电脑。当前包输出一个主导方向，精细C16及增强源码留作扩展。

## 环境与工具

主要设计工具如下；版本取自实际综合报告、HLS报告及编译器记录。

| 工具 | 使用版本 | 用途 |
|---|---|---|
| Vivado | 2020.2，Build 3064766 | BD、RTL/IP集成、综合、布局布线与时序检查 |
| Vitis | 2020.2，ARM GCC 9.2.0 | 裸机BSP、ARM固件与XSCT加载 |
| Vitis HLS | 2020.2，Build 3064766 | C/C++共享计算核，Vivado IP Flow |

原AC880例程、BSP及HLS IP均基于这套工具，保留2020.2以复用已核验的引脚和时序约束。指南推荐的2025.2/2026.1迁移尚未验证。Windows需原厂JTAG驱动；硬件重建需对应AMD工具和IP许可。

PC附属环境：上位机0.5.0-compact-localization，Python 3.13.5、Qt/PySide6 6.8.3、pyqtgraph 0.14.0、NumPy 2.5.3。安装requirements_gui.txt，完整版本和证据位置见build/tool_versions.json。

```cmd
py -3.13 -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements_gui.txt
set "MIC16_PYTHON=%CD%\.venv\Scripts\python.exe"
set "MIC16_XSCT=D:\vivado\Vitis\2020.2\bin\xsct.bat"
set "MIC16_ARM_GCC=D:\vivado\Vitis\2020.2\gnu\aarch32\nt\gcc-arm-none-eabi\bin\arm-none-eabi-gcc.exe"
```

工具安装在其他位置时，只修改上述环境变量。不要把开发者电脑的目录和Python环境当作运行依赖。

## 先做离线检查

```cmd
"%MIC16_PYTHON%" board/runtime.py --check
"%MIC16_PYTHON%" build/rebuild_ps.py --check
"%MIC16_PYTHON%" skill/evidence_audit.py --self-test
"%MIC16_PYTHON%" skill/evidence_audit.py --input skill/examples/valid.json
```

这些命令不连接网络或板卡。sim/保存既有数值和显示检查结果，board/保存实板基线记录。检查文件完整不等于重新进行声学准确率测试。

## 加载与演示

接好电源、JTAG、阵列与网线后，人工运行load_localization_demo.cmd；成功后Qt自动打开，点击“连接 → 开始定位”。同次上电只需软件启停，重开窗口运行run_localization_demo.cmd。当前为临时JTAG运行，断电后需要重载，未写Flash。

电脑网口192.168.1.100，板卡192.168.1.10，其他地址以固件与连接设置为准。摄像头独立打开，使用前关闭VideoCap。定位页为小圆点；视频页可叠加渐变标记，参数只影响显示，不能代替声学标定。

## 从源码重建

```cmd
"%MIC16_PYTHON%" build/rebuild_ps.py --build
```

脚本按归档的146条实际编译命令重建PS，输出位于out/ps/。输入包含原代码、编译时使用的快照、BSP头文件和库；不复用旧应用对象文件。新生成ELF不自动替换board/prebuilt，也不自动下载。工具或路径变化可能改变ELF调试信息，功能和数值仍应检查。

PL复现输入位于fpga/及hardware_source/。当前配套的三种自定义IP（共享频谱2.0、共享计算6.0和Q2.24扫描1.0）已单列在fpga/ip/release/，包含组件描述、RTL和ROM初始化文件。先执行离线依赖检查：

```cmd
"%MIC16_PYTHON%" build/check_pl_inputs.py
```

将目录解压到英文短路径，例如C:\mic16-acoustic-imaging。在Vivado 2020.2中人工创建同一BD的复现工程，随后按需综合实现：

```cmd
vivado.bat -mode batch -source build/rebuild_pl.tcl -tclargs project
vivado.bat -mode batch -source build/rebuild_pl.tcl -tclargs implement
```

project仅恢复BD并生成输出文件；implement才综合和实现。输出在out/pl/；目录已有工程时停止，避免覆盖。两条命令都不连接板卡、不自动下载。HLS对应C/C++快照及版本报告在fpga/hls/release/和build/hls_reports/；当前Vivado入口使用随附已导出的RTL IP，见build/pl_reproduction.md。本次仅进行了文件依赖检查，没有执行上述Vivado命令，也未重跑HLS。原有资源与时序报告是已发布硬件的历史记录，不是新脚本的运行结果。

## 版本与角度

当前预编译ELF来源SHA以board/version.json为准，前半球325槽、289条独立射线、方位15°/板面俯仰7.5°。方位MIC12为0°、MIC0为90°、MIC4为180°、MIC8为270°；PCB平面俯仰0°，正前方90°，法线方位未定义。

历史有限网格基线的Qt平均更新470.339 ms，首次显示1789.910 ms。这些不是当前前半球包的新计时。现有材料没有给出严格角度误差或物理声音变化到画面的延迟。

## 目录对应

| 指南类别 | 本包目录 |
|---|---|
| 设计源码 src | fpga/、firmware/、host/及hardware_source/ |
| 验证 sim | sim/、tests/ |
| 构建与报告 build | build/，含ps_recipe.json与综合实现报告 |
| 上板 board | board/，配套运行文件、加载脚本、既有实测 |
| 数据 data | data/，解析数字报文及参考；不含本次现场录音和相机实拍；build/inputs保留固件内嵌的固定数值参考 |
| 技能 skill | skill/README.md、工作流、案例、审计脚本 |
| 设计报告 report | report/design_report.docx、PDF、正文、流程图和协作记录 |

README.md按指南示例保留标准名称，其余文件路径使用小写ASCII字母、数字、下划线、连字符和扩展名点。中文说明保留在正文。重命名和必要路径调整只作用于提交副本，原开发工程的实现代码与运行文件未改；本次许可证和许可说明已同步到开发目录。

## 授权与提交状态

项目原创源代码及配套代码说明采用[MIT许可证](license.txt)，版权署名为harry123131331。第三方代码、AMD/Xilinx IP、BSP、lwIP、原厂例程及外部依赖保留各自许可，不受本项目MIT重新授权；范围见[第三方说明](third_party_notices.md)和[许可状态](report/license_status.md)。

仓库地址：https://github.com/harry123131331/mic16-acoustic-imaging。定稿代码包已包含MIT许可证和当前设计报告。演示视频、一个双页中英文海报在竞赛系统单独提交，均不放入代码包；参赛承诺与使用授权书由本人填写签署。

## 设计报告阅读索引

报告共41页，第一章说明应用需求、输入与计算速率矛盾、设计方案及测试收益；2.2介绍阵列电路和硬件平台，2.3用九个小节说明FPGA逻辑、共享存储、AXI接口及资源时序，2.4介绍PS调度与Qt声像显示。3.3、3.4给出测试条件、性能及资源对照。正文包含30幅编号图和15条参考文献。

定稿位于report/design_report.docx、report/design_report.pdf；Markdown正文和插图同步更新。49个关键函数的相对路径和行号见report/function_index.md。当前快速路径的公共FFT64由shared_projection承担；保留在BD中的d4f_tf_front_q24不代表本模式同时运行第二条FFT链。文件更新只涉及报告和索引，运行程序、FPGA逻辑和冻结算法保持原版本。

海报为单个双页中英文PPTX，另行上传，不包含在代码包中。演示视频也独立上传，与代码包分开存放。
