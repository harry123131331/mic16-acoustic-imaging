from __future__ import annotations

# 入口计时包含后续Qt/绘图库导入；只记录软件首屏，不冒充设备启动时间。
import os
import time
os.environ['MIC16_GUI_ENTRY_NS']=str(time.perf_counter_ns())
from mic16_qt.app import main


if __name__ == "__main__":
    raise SystemExit(main())

