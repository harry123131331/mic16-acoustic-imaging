# 统计相位分块加速适配器

本模块把四帧、八频点、四候选的相位旋转交给PL，PS保留归一化、频带模长、对数和统计归约。冻结C16及0.65门限不变。当前是离线验证后的候选，尚未完成实板计时或持续调度验收。

## 文件分工

- `mic16_statistics_tile_batch.c`：完整67帧统计的分块调度、观测复用、尾候选及最终归约。静态工作区不可由两次检测并发使用。
- `mic16_statistics_tile_runtime.c`：寄存器、缓存维护、提交／查询和超时隔离。接口不依赖Xilinx库，便于宿主寄存器替身验证。
- `mic16_statistics_tile_runtime_arm.c`：真实Cortex-A9寄存器、cache和计时适配，固定控制地址0x80020000。
- `mic16_statistics_tile_dispatch.c`：开关控制、每块设备计时汇总和链接包装。仅启用时替换`mic16_statistics18_pair_cache`，失败不静默改用CPU。

## 硬件合同

只允许与同一Vivado工程的`statistics_sources`／`statistics_impl`候选配对：HP2、125MHz、64位AXI访存，寄存器偏移以当前导出IP驱动头逐项核对。

64位字实部低32位、虚部高32位。DDR槽依次为：观测1024字、系数1024字、输出256字。所有偏移以64位字计，不是字节。槽首64字节对齐，槽外各有64字节保护区。

旧BIT中0x80020000属于WDAS核，**地址相同不表示接口兼容**。下载前须核对配套BIT／XSA／ELF的SHA256；运行时ABI常量和寄存器回读不能代替这一绑定检查。

## 为什么需要cache维护

PS写入的数据可能还停留在CPUcache中；HP2的PL主口不会自动读取这份cache副本。因此提交前清洁输入，完成后让CPU的输出cache失效，再读取DDR新结果。维护按最多1KiB小段进行，并留协作服务检查点。

同一观测块依次供四组候选使用。后续组只省掉PS打包和观测cache清洁；当前HLS仍每次从DDR加载观测，不存在跨调用片内驻留。

## 所有权与故障

`submit`提交后，CPU不得改写当前槽；`poll`返回1表示仍在执行，返回0才可读取和重用。`ap_done`可能读清且早于`ap_idle`，驱动记住已看到的完成信号，仍等待idle、维护cache并核对保护区后才归还所有权。

超时、时钟倒退、输出越界、非有限值及核返回错误会隔离工作区。不得清零软件状态后继续复用可能仍被硬件写入的内存；须先实际停止或复位硬件。

当前`m16_tile_batch_compute`仍逐块协作等待，并非跨窗异步流水。下一步双槽状态机需要同时拆分准备、提交和归约；单纯调用`submit`不能使整个应用自动实时。

## 验证与候选入口

宿主测试位于`tests/statistics_tile`；寄存器替身会调用真实HLS C模型，但不模拟AXI时序。实板固定输入对照程序位于`build/headless/statistics_batch_arm_final_20261024`，配套包位于`build/hardware/statistics_architecture_20261024/export_binding_final`。

完整证据及限制见`docs/statistics_batch_integration_20261024.md`；文献对应关系见`docs/fpga_conference_architecture_findings_20261024.md`。不得将编译、宿主数值或HLS周期估计表述成实板耗时达标。
