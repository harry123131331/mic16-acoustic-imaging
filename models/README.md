# 冻结模型索引

| 模型 | 参数量 | 位置与作用 |
|---|---:|---|
| C16候选检测器 | 29441 | `host/release/pc_incremental_c16_frozen_20261015/model.pt`，评估物理前端候选，不做声音类别识别 |
| 逐源保守增强器 | 2809 | `host/results/multisource_equivalent_pc_package_20261009/best_checkpoint.pt`，每个当前确认源分别执行后滤波 |

C16权重SHA256为`28fd34b05baaa94f5b62efb86a27604c5a4557668cd6f9e1a94eca26ae48c905`。增强器来源与权重SHA见同目录`model.json`。固件内对应常数分别在`firmware/modules/c`和`firmware/modules/enhancement`中。

不在本目录再复制一份权重。保留内部清单路径是为了继续验证冻结来源，模型加载会检查原有哈希。当前整理没有训练，也没有改变门限或参数。旧研究实验及负结果在原研究工程中保存，不混入日常提交入口。

提交副本已将自编Python注释中文化，依赖清单同时保存原源码哈希与当前注释版哈希；完整AST相同，权重哈希不变。可核对`docs/evidence/python_comment_translation_audit.json`，原研究工程清单未改动。
