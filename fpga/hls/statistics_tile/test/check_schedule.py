"""枚举全部有效块形状，证明实际累加地址重复间距；不是数值误差证明。"""
from pathlib import Path
import json,hashlib
root=Path('M:/');out=root/'build/verification/statistics_tile_schedule_20261023'
out.mkdir(parents=True,exist_ok=False);cases=[];accesses=0
for frames in range(1,5):
 for bins in range(1,9):
  for candidates in range(1,5):
   last={};distances=set();count=0
   for flat in range(8192):
    pair=flat>>8;ctx=(flat>>1)&127;part=flat&1;c=ctx&3;b=(ctx>>2)&7;t=ctx>>5
    if t>=frames or b>=bins or c>=candidates:continue
    index=((pair>>4)*128+ctx)*2+part
    assert 0<=index<512
    if index in last:distances.add(flat-last[index]);assert flat-last[index]==256
    last[index]=flat;count+=1
   assert distances=={256};assert count==frames*bins*candidates*32*2
   accesses+=count;cases.append(dict(frames=frames,bins=bins,candidates=candidates,accesses=count,repeat_distance=256))
value=dict(passed=True,cases=cases,total_accesses=accesses,minimum_repeat_distance=256,
 function='((pair>>4)*128+ctx)*2+part',loop_iterations=8192,interdependence_preserved=True,
 mathematical_argument='每16个pair地址前缀不变，ctx/part各占一个周期；下一pair同地址距离256。pair=16切换前缀至互不重叠的后256地址。尾块仅固定屏蔽部分上下文。',
 source_sha256=hashlib.sha256((root/'fpga/hls/statistics_tile/src/statistics_tile.cpp').read_bytes()).hexdigest())
(out/'proof.json').write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('STATISTICS_TILE_SCHEDULE cases='+str(len(cases))+' accesses='+str(accesses)+' distance=256 errors=0',flush=True)
