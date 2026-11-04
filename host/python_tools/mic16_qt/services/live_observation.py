"""仅统计接收端连续性，不虚构采样时刻、计算耗时或端到端实时性。"""
from collections import deque
import math
import time

class LiveObservation:
    def __init__(self):
        self.reset()
    def reset(self):
        self.epoch=None;self.last_window=None;self.times=deque(maxlen=32)
        self.windows=0;self.skipped=0
    def observe(self,epoch,window,now=None):
        now=time.monotonic() if now is None else now
        if not math.isfinite(now) or not isinstance(window,int) or window<0:
            raise ValueError('持续统计的窗号或时刻非法')
        if self.epoch is not None and epoch!=self.epoch:self.reset()
        if self.last_window is not None:
            if window<=self.last_window:return False
            self.skipped+=max(0,window-self.last_window-1)
            if self.times and now<self.times[-1]:raise ValueError('单调时钟倒退')
        self.epoch=epoch;self.last_window=window;self.times.append(now);self.windows+=1
        return True
    def text(self,now=None):
        now=time.monotonic() if now is None else now
        if not self.times:return '定位：等待持续数据 · 端到端延迟未核验'
        age=max(0.,now-self.times[-1])
        if self.windows==1:return f'仅收到1窗，尚无持续输出 · 接收后{age:.1f}s · 端到端延迟未核验'
        span=self.times[-1]-self.times[0]
        rate=(len(self.times)-1)/span if span>0 else 0.
        return f'定位接收{rate:.2f}次/秒 · 接收后{age:.1f}s · 跳窗{self.skipped} · 端到端延迟未核验'
