# MIC16 Python 上位机源码

更新时间：2026-08-10

本目录维护 MIC16 的 PC 端采集、诊断、GCC-PHAT 定位、鲁棒多通道增强和 MATLAB 导出代码。正式参数固定为：

```text
sample_rate = 39062.5 Hz
channels = 16
frame_samples = 2048
fft_size = 4096
array_radius = 0.080 m
GCC-PHAT = 32 pairs x 41 lags (-20 ... +20)
angle_classes = 72 (0 ... 355 deg, step 5 deg)
```

## 1. 只使用两个入口

日常板卡采集与查看结果：

```powershell
python mic16_gui.py
```

自动测试、批处理和可复现实验：

```powershell
python mic16_experiment_runner.py --help
```

其他 `mic16_*.py` 是被这两个入口调用的模块或诊断脚本，不需要逐个启动。

## 2. 一体化带标签录制

GUI 的“实验与导出”页已经集成：

```text
填写标签
-> 录制 raw_i32
-> UDP 连续性和数值门禁
-> recordings_manifest.csv
-> MAT 导出
-> MATLAB R2024a 图、CSV 和结果索引
```

命令行等价示例：

```powershell
python mic16_experiment_runner.py record-labeled `
  --duration 20 `
  --record-id real_val_a010_001 `
  --dataset-role real_val `
  --source-type loudspeaker `
  --signal-type speech `
  --session-id real_val_20260810 `
  --scene-id office `
  --speaker-id speaker_01 `
  --label-azimuth-deg 10 `
  --label-range-m 1.0 `
  --output-root ..\data\recordings `
  --recordings-manifest ..\data\recordings\recordings_manifest.csv `
  --minimal-output `
  --auto-matlab-report `
  --matlab-exe D:\matlab\bin\matlab.exe
```

## 3. 输出与门禁

每条正式录音至少保留：

```text
*_raw_i32.npy
*_metadata.json
*_packet_manifest.csv
*_segments.csv
*_recording_manifest.json
recordings_manifest.csv
```

三个状态分别表示：

- `CAPTURE_VALID`：PCM 数值和 UDP 连续性通过。
- `MAT_EXPORTED`：MAT 文件生成成功。
- `MATLAB_RENDERED`：MATLAB 图与 CSV 生成成功。

int32 上下限饱和、数值种类过少、常量通道、丢包/乱序等异常会写入 metadata 和 manifest。无效录音可保留做故障诊断，但不能进入训练或论文指标。

## 4. 正式模块分层

| 层 | 关键文件 |
|---|---|
| GUI/编排 | `mic16_gui.py`、`mic16_experiment_runner.py` |
| 采集/协议 | `mic16_udp_common.py`、`mic16_record_10s_fft.py` |
| 标签/审计 | `mic16_capture_pipeline.py`、`mic16_recording_manifest.py` |
| 配置/几何 | `mic16_config.py`、`mic16_channel_map.py`、`mic16_geometry_convention.py` |
| 正式特征 | `mic16_gcc_phat_features.py` |
| 传统诊断 | `mic16_traditional_ssl.py` |
| MATLAB | `mic16_export_matlab.py`、`../matlab_tools/plot_mic16_capture_report.m` |
| PC Proposed | `gui/formal_pc_localizer.py` 及冻结 checkpoint/metadata |

历史 NCC 代码不作为默认训练、推理或部署入口。

## 5. 测试

```powershell
python -m unittest discover -s tests -p "test_*.py" -v
```

端到端软件冒烟测试可使用 `record-labeled --synthetic --auto-matlab-report`。它只证明软件工作流，不代表真实阵列定位精度或板端结果。

完整 GUI 操作和路径说明见：

```text
../docs/MIC16_PC上位机一体化采集与报告使用说明.md
../docs/MIC16_PC上位机工程审计与打包说明.md
```
