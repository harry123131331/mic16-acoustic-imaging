# 批量共享投影引擎

当前源码/PS协议是版本3，同一局部存储和计算通路按带标签任务执行统计相位旋转、扫描成对归约、物理响应顺序FP64累加。单批最多8个任务。该核已实现，不等于完整新任务图或实时性能已经通过。

## 数值与接口

统计保持原32对的顺序短/长基线FP32累加；扫描保持原成对树、奇数尾项及FTZ，返回未归一化FP32和。精度敏感的归一化、对数和不确定方向回退仍留PS。物理模式保留FP32非正规数，以精确48位乘积编码及普通FP64加法维持原逐对/逐频点求和顺序；不是FMA或改写求和树。最多3帧×34频点×16候选，返回原FP64累加位，PS先转FP32再按原式相除。

描述符和布局见src/shared_projection_engine.h。全部跨度、跨任务别名、保留位和标签由真实PS预检；硬件重复基础范围检查。runtime_submit仅登记，poll每次最多维护1KiB缓存或检查一次设备；done、idle、失效缓存、标签均通过后才可retire。错误/超时隔离不伪造硬件空闲。

## 当前证据

- build/verification/shared_projection_physical_shared_product_20261024/：70,871个既有浮点分量及540个新增FP64物理响应逐位一致。
- build/verification/projection_runtime_physical_shared_product_20261024/：18项真实C驱动与ARM编译通过，CPU缓存/设备RAM分离。
- build/hls/shared_projection_physical_20261024/：版本3四个混合任务的Verilog联仿通过，252,978周期，IP导出VLNV为mic16:acoustic:m16_shared_projection_engine:3.0。
- build/verification/physical_projection_task_faults_20261024/：物理任务节点完成全24频带/20帧的12,000个原C响应逐位对照，14项检查通过；详见docs/physical_projection_task_20261024.md。

HLS估计12,372 LUT、14,119 FF、14 DSP、36个18K BRAM；估计6.668ns，8ns减2.16ns后的有效预算5.84ns未过。循环II通过、C/RTL一致和IP导出均不能替代整机时序。测试台原分配40,000字但接口包装深度524,288导致越界，已只补足测试台数组，逻辑合法窗口仍40,000字，核实现哈希不变；失败日志保留。

版本2的资源底座整机布线已过setup +0.064ns/hold +0.015ns/DRC零错误，具体版本2源码保留于build/hls/shared_projection_registered_control_20261024/source_snapshot/。同一BD版本3也已完成实布线：setup +0.069ns、hold +0.015ns、DRC零错误；Slice12,977、BRAM123、DSP149。其CDC与版本2一致，19条继承Critical未关闭。默认正式BIT/XSA/ELF未替换。

2020.2日期导出溢出仅修本次生成core_revision元数据，未改RTL、工具安装或系统时间。参考[AMD AR76960](https://docs.amd.com/r/en-US/76960)、[2020.2资源共享](https://docs.amd.com/r/2020.2-English/ug1399-vitis-hls/pragma-HLS-allocation)。完整架构边界和未完成项见docs/realtime_architecture_decision_20261024.md。
