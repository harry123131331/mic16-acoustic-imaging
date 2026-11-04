"""类别无关声学结果与视觉区域的辅助关联；输入视觉框，不负责运行物体检测网络。"""
from __future__ import annotations
from dataclasses import dataclass
import math
from .video_wire import VideoMeta,TIME_VERIFIED

@dataclass(frozen=True)
class Source:
    source_id:int
    azimuth_deg:float
    elevation_deg:float
    confirmed:bool=True

@dataclass(frozen=True)
class AudioObservation:
    epoch:int
    window:int
    start_us:int
    end_us:int
    clock_domain:int
    time_verified:bool
    sources:tuple[Source,...]

@dataclass(frozen=True)
class Box:
    object_id:int
    x1:float
    y1:float
    x2:float
    y2:float
    score:float
    label:str=''

@dataclass(frozen=True)
class Alignment:
    width:int
    height:int
    mount_id:int
    mode_id:int
    transform_id:int
    u_coeff:tuple[float,float,float]
    v_coeff:tuple[float,float,float]
    origin:str
    alignment_id:int

def _overlap(a,b):
    return max(0.,min(a[2],b[2])-max(a[0],b[0]))*max(0.,min(a[3],b[3])-max(a[1],b[1]))

def associate(audio:AudioObservation,video:VideoMeta,alignment:Alignment|None,boxes:tuple[Box,...],
              *,boxes_frame_id:int|None,now_us:int,max_age_us=200000,tolerance_us=50000,
              azimuth_radius_deg=5.,elevation_radius_deg=5.,min_box_score=.25,allow_synthetic=False):
    """保留每个声源和原方向，返回可绘制的近似方向/关联区域；无视觉匹配不删声源。
    不估计距离，不把人/音箱标签当声音类别，不把框中心当独立定位真值。
    """
    if (not isinstance(now_us,int) or not isinstance(max_age_us,int) or max_age_us<=0 or
        not isinstance(tolerance_us,int) or tolerance_us<0 or len(audio.sources)>8 or audio.start_us<0 or
        audio.end_us<=audio.start_us or audio.epoch<=0 or audio.window<0 or
        not math.isfinite(azimuth_radius_deg) or not 0<azimuth_radius_deg<=30 or
        not math.isfinite(elevation_radius_deg) or not 0<elevation_radius_deg<=30 or not 0<=min_box_score<=1):
        raise ValueError('声像关联参数无效')
    ids=set()
    for s in audio.sources:
        if (s.source_id in ids or not 1<=s.source_id<=65535 or
            not math.isfinite(s.azimuth_deg) or not -60<=s.azimuth_deg<=60 or
            not math.isfinite(s.elevation_deg) or not -30<=s.elevation_deg<=30):raise ValueError('声源元数据无效')
        ids.add(s.source_id)
    reason=None
    if alignment is None:reason='NO_ALIGNMENT'
    elif (alignment.origin not in ('REAL_APPROXIMATE_ALIGNMENT','SYNTHETIC_TEST') or
          alignment.origin=='SYNTHETIC_TEST' and not allow_synthetic or not alignment.alignment_id):reason='ALIGNMENT_UNVERIFIED'
    elif (len(alignment.u_coeff)!=3 or len(alignment.v_coeff)!=3 or
          not all(math.isfinite(v) for v in alignment.u_coeff+alignment.v_coeff)):
        raise ValueError('近似对齐系数无效')
    elif (alignment.width,alignment.height,alignment.mount_id,alignment.mode_id,alignment.transform_id)!=(
          video.width,video.height,video.mount_id,video.mode_id,video.transform_id):reason='GEOMETRY_MISMATCH'
    elif audio.epoch!=video.epoch:reason='EPOCH_MISMATCH'
    elif (not audio.time_verified or not video.flags&TIME_VERIFIED or
          not audio.clock_domain or audio.clock_domain!=video.clock_domain):reason='TIME_UNVERIFIED'
    elif (now_us<max(audio.end_us,video.capture_us) or now_us-audio.end_us>max_age_us or
          now_us-video.capture_us>max_age_us):reason='STALE_OR_FUTURE'
    elif not audio.start_us-tolerance_us<=video.capture_us<=audio.end_us+tolerance_us:reason='TIME_MISMATCH'
    usable=[]
    if boxes_frame_id==video.frame_id:
        boxids=set()
        for b in boxes:
            if (b.object_id in boxids or b.object_id<0 or not all(math.isfinite(v) for v in (b.x1,b.y1,b.x2,b.y2,b.score)) or
                not 0<=b.score<=1 or not 0<=b.x1<b.x2<=video.width or not 0<=b.y1<b.y2<=video.height):raise ValueError('视觉框无效')
            boxids.add(b.object_id)
            if b.score>=min_box_score:usable.append(b)
    output=[]
    for s in audio.sources:
        row=dict(source_id=s.source_id,azimuth_deg=s.azimuth_deg,elevation_deg=s.elevation_deg,
                 confirmed=s.confirmed,status=reason or 'AUDIO_ONLY',audio_pixel=None,audio_region=None,
                 object_ids=[],visual_region=None,alignment_id=alignment.alignment_id if alignment else None,
                 approximate=True,identity_of_sounding_object_claimed=False)
        if reason or not s.confirmed:
            if not reason:row['status']='TRACK_HELD_NOT_CURRENT'
            output.append(row);continue
        def project(az,el):
            return (alignment.u_coeff[0]+alignment.u_coeff[1]*az+alignment.u_coeff[2]*el,
                    alignment.v_coeff[0]+alignment.v_coeff[1]*az+alignment.v_coeff[2]*el)
        u,v=project(s.azimuth_deg,s.elevation_deg)
        if not 0<=u<video.width or not 0<=v<video.height:
            row['status']='OUTSIDE_CAMERA';output.append(row);continue
        corners=[project(s.azimuth_deg+da,s.elevation_deg+de) for da in (-azimuth_radius_deg,azimuth_radius_deg) for de in (-elevation_radius_deg,elevation_radius_deg)]
        region=(max(0.,min(c[0] for c in corners)),max(0.,min(c[1] for c in corners)),
                min(float(video.width),max(c[0] for c in corners)),min(float(video.height),max(c[1] for c in corners)))
        matched=[b for b in usable if _overlap(region,(b.x1,b.y1,b.x2,b.y2))>0]
        row.update(audio_pixel=(u,v),audio_region=region)
        if matched:
            row.update(status='ASSOCIATED_REGION',object_ids=[b.object_id for b in matched],
                       visual_region=(min(b.x1 for b in matched),min(b.y1 for b in matched),max(b.x2 for b in matched),max(b.y2 for b in matched)))
        elif boxes_frame_id!=video.frame_id:row['status']='AUDIO_ONLY_NO_CURRENT_BOXES'
        output.append(row)
    return output
