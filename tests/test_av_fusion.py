"""C到Python视频互通、失包/坏包/顺序/内存边界及类别无关区域关联。"""
from dataclasses import replace
from pathlib import Path
import argparse,hashlib,json,random,socket,struct,sys,zlib
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'host/python_tools'))
from mic16_av_fusion.video_wire import HEADER,VideoAssembler,VideoFrame,VideoMeta,decode_packet,to_rgb888
from mic16_av_fusion.association import Alignment,AudioObservation,Box,Source,associate

checks=0
def check(ok):
    global checks
    checks+=1
    if not ok:raise AssertionError('检查失败：'+str(checks))
def rejected(action):
    try:action()
    except ValueError:check(True)
    else:check(False)
def change(packet,**changes):
    fields=list(HEADER.unpack_from(packet));indices=dict(epoch=3,frame=4,capture=5,width=6,height=7,fmt=8,flags=9,mount=10,mode=11,domain=12,total=13,offset=14,frame_crc=15,index=16,chunks=17,size=18,reserved=19,transform=20)
    for key,value in changes.items():fields[indices[key]]=value
    head=HEADER.pack(*fields);body=packet[72:];fields[-1]=zlib.crc32(body,zlib.crc32(head[:68]))&0xffffffff
    return HEADER.pack(*fields)+body
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--fixture',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    data=a.fixture.read_bytes();packets=[];offset=0
    while offset<len(data):
        n=struct.unpack_from('<I',data,offset)[0];offset+=4;packets.append(data[offset:offset+n]);offset+=n
    check(offset==len(data) and len(packets)==7 and HEADER.size==72)
    for i,packet in enumerate(packets):
        m,index,chunks,at,body=decode_packet(packet)
        check(index==i and chunks==7 and at==i*1000 and len(body)==min(1000,6144-at))
        check(m.epoch==19 and m.width==64 and m.height==48 and m.format==1 and m.capture_us==1000000)
    expected=bytes((i*37+11)&255 for i in range(6144));peer=('127.0.0.1',41000)
    assembler=VideoAssembler();complete=None
    order=[6,0,2,2,1,5,4,3]
    for i,index in enumerate(order):
        frame=assembler.push(packets[index],peer,now_s=i*.001)
        check(frame is None or complete is None)
        if frame is not None:complete=frame
        check(assembler.pending_payload_bytes<=2*6144)
    check(complete is not None and complete.pixels==expected and assembler.stats['duplicates']==1)
    check(assembler.push(packets[0],peer,now_s=.1) is None)
    rejected(lambda:VideoAssembler().push(packets[0][:-1],peer,now_s=0))
    bad=bytearray(packets[0]);bad[-1]^=1
    rejected(lambda:VideoAssembler().push(bad,peer,now_s=0))
    for changes in [dict(width=0),dict(width=801),dict(height=481),dict(fmt=99),dict(flags=2),dict(domain=0),
                    dict(offset=1),dict(chunks=8),dict(size=999),dict(reserved=1),dict(total=99999999)]:
        rejected(lambda c=changes:VideoAssembler().push(change(packets[0],**c),peer,now_s=0))
    session=VideoAssembler();session.push(packets[0],peer,now_s=0)
    rejected(lambda:session.push(packets[1],('127.0.0.2',41000),now_s=.01))
    rejected(lambda:session.push(change(packets[1],epoch=20),peer,now_s=.02))
    rejected(lambda:session.push(change(packets[1],mount=99),peer,now_s=.03))
    session=VideoAssembler(timeout_s=.1);session.push(packets[0],peer,now_s=0)
    check(session.push(packets[1],peer,now_s=.2) is None and session.stats['expired']==1)
    check(session.stats['completed']==0)
    session=VideoAssembler()
    for identity in range(23,123):session.push(change(packets[0],frame=identity),peer,now_s=identity*.001)
    check(len(session.pending)<=2 and session.pending_payload_bytes<=12288)
    rejected(lambda:session.push(packets[0],peer,now_s=0))
    session=VideoAssembler()
    for packet in packets[:-1]:session.push(change(packet,frame_crc=1),peer,now_s=0)
    rejected(lambda:session.push(change(packets[-1],frame_crc=1),peer,now_s=.01))
    session=VideoAssembler();session.push(packets[0],peer,now_s=0)
    different=bytearray(packets[0]);different[72]^=1;fields=list(HEADER.unpack_from(different));fields[-1]=zlib.crc32(different[72:],zlib.crc32(different[:68]))&0xffffffff
    rejected(lambda:session.push(HEADER.pack(*fields)+different[72:],peer,now_s=.01))
    session=VideoAssembler();clock=0.
    for identity in (0xfffffffe,0xffffffff,0):
        got=None
        for packet in packets:
            clock+=.001;event=session.push(change(packet,frame=identity),peer,now_s=clock)
            if event is not None:got=event
        check(got is not None and got.meta.frame_id==identity and got.pixels==expected)
    check(session.push(change(packets[0],frame=0xffffffff),peer,now_s=clock+.01) is None)
    session=VideoAssembler()
    for packet in packets[:-1]:check(session.push(packet,peer,now_s=0) is None)
    check(session.stats['completed']==0 and session.pending_payload_bytes==6144)
    # 本机UDP仅验证真实socket到组帧的连接，不代表板卡网口已回传图像。
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as receiver,socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as sender:
        receiver.bind(('127.0.0.1',0));receiver.settimeout(1);session=VideoAssembler();got=None
        for packet in reversed(packets):
            sender.sendto(packet,receiver.getsockname());received,source=receiver.recvfrom(2048)
            event=session.push(received,source)
            if event is not None:got=event
        check(got is not None and got.pixels==expected)
    # 固定像素真值检查字节序；并非摄像头实际画面。
    color_meta=replace(complete.meta,width=3,height=1,total_bytes=6)
    colors=to_rgb888(VideoFrame(color_meta,b'\x00\xf8\xe0\x07\x1f\x00',0))
    check(colors.tolist()==[[[255,0,0],[0,255,0],[0,0,255]]])
    m=complete.meta
    alignment=Alignment(64,48,1,2,4,(32.,.5,0.),(24.,0.,-.5),'SYNTHETIC_TEST',9)
    audio=AudioObservation(19,2,900000,1050000,3,True,(Source(1,0,0),Source(2,40,0)))
    boxes=(Box(10,29,21,32,27,.9,'人'),Box(11,32,21,35,27,.9,'音箱'))
    kwargs=dict(boxes_frame_id=23,now_us=1100000,allow_synthetic=True)
    rows=associate(audio,m,alignment,boxes,**kwargs)
    check(len(rows)==2 and [r['source_id'] for r in rows]==[1,2])
    check(rows[0]['object_ids']==[10,11] and rows[0]['audio_pixel']==(32.,24.))
    check(rows[1]['status']=='AUDIO_ONLY' and rows[1]['object_ids']==[])
    check(all(not r['identity_of_sounding_object_claimed'] for r in rows))
    rows=associate(audio,m,alignment,boxes,boxes_frame_id=22,now_us=1100000,allow_synthetic=True)
    check(rows[0]['status']=='AUDIO_ONLY_NO_CURRENT_BOXES')
    for metadata,profile,observation,now,reason in [
        (m,alignment,audio,1400000,'STALE_OR_FUTURE'),(replace(m,flags=0),alignment,audio,1100000,'TIME_UNVERIFIED'),
        (m,alignment,replace(audio,clock_domain=8),1100000,'TIME_UNVERIFIED'),
        (m,replace(alignment,mount_id=8),audio,1100000,'GEOMETRY_MISMATCH'),
        (m,None,audio,1100000,'NO_ALIGNMENT'),(m,alignment,replace(audio,epoch=20),1100000,'EPOCH_MISMATCH')]:
        rows=associate(observation,metadata,profile,boxes,boxes_frame_id=23,now_us=now,allow_synthetic=True)
        check(len(rows)==2 and all(r['status']==reason and r['audio_pixel'] is None for r in rows))
    rows=associate(audio,m,alignment,boxes,boxes_frame_id=23,now_us=1100000)
    check(all(r['status']=='ALIGNMENT_UNVERIFIED' for r in rows))
    held=replace(audio,sources=(Source(1,0,0,False),))
    check(associate(held,m,alignment,boxes,**kwargs)[0]['status']=='TRACK_HELD_NOT_CURRENT')
    rejected(lambda:associate(replace(audio,sources=(Source(1,0,0),Source(1,1,1))),m,alignment,boxes,**kwargs))
    result=dict(status='PASS_C_VIDEO_TO_HOST_AND_OFFLINE_REGION_ASSOCIATION',checks=checks,C_packets=7,
        reconstructed_bytes=len(expected),fixture_sha256=hashlib.sha256(data).hexdigest(),localhost_socket_verified=True,
        video_memory_bounded=True,visual_boxes_origin='SYNTHETIC_TEST_INPUT_NOT_DETECTOR_OUTPUT',
        physical_camera_network_verified=False,visual_neural_detector_executed=False,GUI_integrated=False,
        physical_sync_verified=False,acoustic_accuracy_measured=False,whole_system_accepted=False,
        overlay_example=associate(audio,m,alignment,boxes,**kwargs))
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({k:v for k,v in result.items() if k!='overlay_example'},ensure_ascii=False))
if __name__=='__main__':main()
