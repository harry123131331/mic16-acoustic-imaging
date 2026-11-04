# 共享FFT候选纯频谱原型

> PS接入更新：批量/驱动/分派回放与可选ARM链接已通过，尚未实板执行；当前状态见 docs/shared_spectrum_ps_integration.md。下文保留先前阶段的范围。

> 新增模式4：输入改为每个变换2048个F32连续存放，两个样本/64位word；PS配套函数为m16_shared_pack_contiguous。模式1仍使用下述16列交错布局。数值、综合与模式4最大批量RTL已通过，尚未接入ARM。连续模式报告：docs/shared_spectrum_contiguous_results.md。

该目录在统一工程内复用原4096点FFT引擎，增加候选波形的纯频谱模式。正式BIT/XSA及应用入口尚未更换，当前不支持512点统计模式。

PS负责原FP64去均值、对称Hann窗及F32转换；PL输出正频点32至838的807个复数，跳过PHAT与转置。公共定位FFT先保留PS的原FP64实现。该模式的HLS综合只生成一个FFT函数实例，完整验收状态见同工程 `docs/shared_spectrum_candidate_results.md`。

## 接口与内存

- 输入为2048行、16列sample-major F32；索引 `sample*16+channel`，每个64位word存两个F32，偶数索引位于低32位。
- `active_channels` 为1至16，非活动列不计算。输入已经加窗，不可再次去均值、乘窗或按整数PCM解释。
- 输出位于 `workspace_word_offset+channel*807+bin`；每个64位word实部低32位、虚部高32位，bin=0对应FFT频点32。正向FFT不做4096归一化。
- 完整兼容arena为80944个64位word：输入16384word，workspace默认从16384开始，保留64560word。模式1只写活动列频谱，不写PHAT scratch。分配和所有权仍由PS管理。
- 模式0保留旧整数数据计算；模式1要求frame_samples=2048。未知模式、活动列0/大于16或模式1帧长错误时返回-1且不写内存。模式0仍保持原帧长规则。

## 控制寄存器

地址来自本轮HLS生成的控制RTL，不能沿用旧前端驱动的地址常量。新top增加返回值和参数，接入时须与新BIT/XSA同时更新。

| 字节偏移 | 字段 |
|---|---|
| 0x00 | ap_ctrl |
| 0x04/0x08/0x0c | GIE/IER/ISR |
| 0x10 | 返回值，0成功、-1参数错误 |
| 0x18/0x1c | arena基地址低/高32位 |
| 0x24 | 输入word偏移 |
| 0x2c | workspace word偏移 |
| 0x34 | frame_samples |
| 0x3c | mode |
| 0x44 | active_channels |

控制地址以字节计，输入及workspace偏移以64位word计，二者不可混用。完整机器可读表在 `build/hls/shared_spectrum_20261023/abi_contract.json`。

PS接入前必须验证有限输入、地址/长度/对齐及不重叠，保留原最大幅值非零且小于1e-15或大于2的FP64回退。完成前不得复用arena；缓存维护、内存屏障及超时后核状态处理仍需在板端驱动实现。本原型尚不能被当作已经接入ARM的加速路径。

## 离线证据

`build/hls/shared_spectrum_20261023` 保存协议、厂商位精确模型、完整C16回放、HLS报告和C/RTL记录。普通/强弱/三源/噪声/双窄带六类输入均为固定数值迁移检查，不能当作新房间准确率或实板时间。

候选输入目前仍为跨列步进访问，综合提示存在未成突发的AXI访问。PS批处理、连续布局和DDR实测仍是后续优化项目；不能把理想HLS周期直接相加当作整机耗时。

参考原理：[AMD UG1399 ALLOCATION 2020.2](https://docs.amd.com/r/2020.2-English/ug1399-vitis-hls/pragma-HLS-allocation) 明确共享实例以资源换并行度；实例数仍须用综合RTL核对。
