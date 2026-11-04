# 复现检查记录

PS：146个编译单元已用Vitis 2020.2 ARM GCC 9.2.0从源码编译并链接，记录见rebuild_validation.json。脚本默认--check，只有人工加--build才编译。BSP与固定数值参考已包含；未复用旧应用对象文件。新ELF未替换配套预编译运行包。

PL：2026年11月2日补入当前频谱2.0、共享计算6.0和扫描1.0的完整导出IP目录，包含ROM初始化文件；新增相对路径复现入口及只读输入检查。设置、使用步骤和限制见pl_reproduction.md，静态检查见pl_input_validation.json。本轮未调用Vivado/HLS，整套PL冷构建仍待实际验证。

板端：本轮未下载、复位、采集或写Flash。board/prebuilt中的匹配BIT/XSA/ELF保持；ELF去调试副本与原文件的实际分配区和入口一致性沿用原核验结果。

部分历史DLL夹具未分发，相应测试需明确跳过，不能算通过。当前前半球网格的新ARM计时、角度误差和长期动态演示仍需后续实测。
