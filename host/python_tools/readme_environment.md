# MIC16 Python 工具环境说明

本目录可以作为独立 Python 工具包使用，也可以放在完整 MIC16 工程内使用。

## 1. 推荐环境变量

```powershell
$env:MIC16_ROOT="project/mic16_next_version"
$env:MIC16_DATA_ROOT="D:\vscode\python_class\mic16_record_output"
$env:MIC16_SSL_NN_ROOT="project/ssl_nn"
$env:MIC16_VITIS_ROOT="project/mic16_next_version\vitis_src"
```

当前 Python 工具不要求所有变量都存在。`python-only` 检查只验证采集、GCC-PHAT 特征、manifest、MATLAB 导出和 runner 入口；`full-project` 才检查 `ssl_nn / vitis_src / hls_src / vivado_*`。

## 2. 核心依赖

采集、诊断和数据集构建：

```powershell
python -m pip install -r requirements-core.txt
```

训练环境另装：

```powershell
python -m pip install -r requirements-ml.txt
```

## 3. 正式数据链

```text
UDP AUDIO
-> raw_i32.npy
-> packet_manifest.csv / segments.csv
-> recordings_manifest.csv
-> channel_map
-> GCC-PHAT [32,41]
-> features.npy / labels.npy / frames_manifest.csv
```

`raw_i32.npy` 永远保持 UDP 原始通道顺序。所有正式算法输入必须先通过：

```text
channel_map[logical_mic_id] = udp_raw_channel_id
```

得到逻辑 `MIC0..MIC15` 顺序。

## 4. Preflight

独立 Python 工具包：

```powershell
python mic16_preflight_check.py --scope python-only
```

完整工程：

```powershell
python mic16_preflight_check.py --scope full-project
```

缺少外部工程目录时，`python-only` 不应失败；`full-project` 会报告 `not present / not checked`。

## 5. 2026-09-09 完整算法复验环境

本机基础 Anaconda 环境没有 torch；原 `envs/pytorch` 有 torch 但缺 SciPy、Pandas、Matplotlib 和 SoundFile。仅 GUI 测试通过不意味着完整离线评价依赖齐全。

本轮使用 `mic16_next_version/.runtime/pc_eval/Scripts/python.exe`：Python 3.11 虚拟环境，采用 `--system-site-packages` 复用既有 PyTorch，其余评价依赖安装在项目环境中，没有修改全局 Python。运行使用 CPU，不需要新增 CUDA 安装。

- 完整算法依赖入口是 `ssl_nn/requirements.txt`，已补齐 SciPy 和 Pandas。
- 本轮实际版本清单为 `mic16_next_version/results/pc_revalidation_20260909/environment_freeze.txt`，它是环境记录；其中 CUDA 版 PyTorch 不能假定可从默认 PyPI 直接安装。
- `.runtime/`、`.venv/` 不属于工程源码，不纳入比赛包/PC 源码包。
- 每次连续评价保存 `reproduction_manifest.json` 与 `source_snapshot.zip`，记录实际参数、解释器和源码哈希；旧结果不会被覆盖。
