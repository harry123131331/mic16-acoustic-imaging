"""独立解码快照状态机产生的C包，检查内容、CRC和未核验时间语义。"""
from pathlib import Path
import sys,json,struct,zlib
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'host/python_tools'))
from mic16_av_fusion.video_wire import VideoAssembler,decode_packet
source=Path(sys.argv[1]);output=Path(sys.argv[2]);data=source.read_bytes();at=0;packets=[];checks=0
def check(ok):
    global checks
    checks+=1
    if not ok:raise AssertionError(checks)
while at<len(data):
    n=struct.unpack_from('<I',data,at)[0];at+=4;packets.append(data[at:at+n]);at+=n
first_meta=decode_packet(packets[0])[0]
size=first_meta.total_bytes
check(at==len(data) and len(packets)==(size+999)//1000)
assembler=VideoAssembler();frame=None
for i,packet in enumerate(reversed(packets)):
    meta,index,chunks,offset,payload=decode_packet(packet)
    check(meta.frame_id==23 and meta.epoch==19 and (meta.width,meta.height) in ((64,48),(800,480)))
    check(meta.flags==0 and meta.capture_us==0 and meta.clock_domain==0)
    check(meta.mount_id==1 and meta.mode_id==2 and meta.transform_id==4)
    item=assembler.push(packet,('127.0.0.1',5002),now_s=i*.0001)
    if item is not None:frame=item
expected=bytes((i*37+11)&255 for i in range(size))
check(frame is not None and frame.pixels==expected)
check(frame.meta.frame_crc32==zlib.crc32(expected)&0xffffffff)
check(assembler.stats['completed']==1 and assembler.pending_payload_bytes==0)
if size>6144:
    slow=VideoAssembler();published=None
    for i,packet in enumerate(reversed(packets)):
        item=slow.push(packet,('127.0.0.1',5002),now_s=i*.001)
        if item is not None:published=item
    check(published is None and slow.stats['expired']>=1)
result=dict(status='PASS_PS_SNAPSHOT_C_TO_PYTHON',checks=checks,packet_count=len(packets),frame_bytes=size,
            injected_packet_interval_s=.0001,physical_capture_time_claimed=False,pixels_from_controlled_device=True,board_operations=0)
output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');print(json.dumps(result))
