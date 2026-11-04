"""仅用于人工粗对齐的针孔方向预览；不估计距离，不替代严格声像标定。"""
from __future__ import annotations
from dataclasses import dataclass,asdict,replace
import math

@dataclass(frozen=True)
class PreviewAlignment:
    yaw_deg:float=0.
    pitch_deg:float=0.
    horizontal_fov_deg:float=60.
    vertical_fov_deg:float=45.
    offset_u:float=0.
    offset_v:float=0.
    radius_deg:float=3.
    mirror_x:bool=False
    mirror_y:bool=False
    roll_deg:float=0.

    def validate(self):
        for key,value in asdict(self).items():
            if key.startswith('mirror_'):
                if type(value)is not bool:raise ValueError('镜像参数必须为布尔值')
            elif isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value):raise ValueError('粗对齐参数必须为有限数值')
        if not(-180<=self.yaw_deg<=180 and -89<=self.pitch_deg<=89 and -180<=self.roll_deg<=180 and
               5<=self.horizontal_fov_deg<=170 and 5<=self.vertical_fov_deg<=170 and
               -.5<=self.offset_u<=.5 and -.5<=self.offset_v<=.5 and .1<=self.radius_deg<=15):
            raise ValueError('粗对齐参数超出范围')

def project_direction(azimuth_deg,elevation_deg,alignment:PreviewAlignment):
    """返回画面归一化坐标与显示半径；越界/背向返回None，绝不吸附到边缘。
    方向单位向量经相机偏航/俯仰与安装滚转旋转，再按针孔模型投影。
    相机与阵列平移和镜头畸变未补偿，因此返回值只能作近似方向标记。
    """
    alignment.validate()
    if not all(isinstance(v,(int,float)) and not isinstance(v,bool) and math.isfinite(v) for v in (azimuth_deg,elevation_deg)):
        raise ValueError('声学方向不是有限数值')
    if not -180<=azimuth_deg<=180 or not -90<=elevation_deg<=90:raise ValueError('声学方向越界')
    az,el,yaw,pitch=map(math.radians,(azimuth_deg,elevation_deg,alignment.yaw_deg,alignment.pitch_deg))
    x,y,z=math.cos(el)*math.sin(az),math.sin(el),math.cos(el)*math.cos(az)
    x,z=math.cos(yaw)*x-math.sin(yaw)*z,math.sin(yaw)*x+math.cos(yaw)*z
    y,z=math.cos(pitch)*y-math.sin(pitch)*z,math.sin(pitch)*y+math.cos(pitch)*z
    if alignment.roll_deg:
        # 正角将阵列右轴转向画面上方；零角分支保持原投影的运算顺序。
        roll=math.radians(alignment.roll_deg)
        x,y=math.cos(roll)*x-math.sin(roll)*y,math.sin(roll)*x+math.cos(roll)*y
    if z<=1e-9:return None
    fx=.5/math.tan(math.radians(alignment.horizontal_fov_deg)/2)
    fy=.5/math.tan(math.radians(alignment.vertical_fov_deg)/2)
    u=.5+fx*x/z;v=.5-fy*y/z
    if alignment.mirror_x:u=1-u
    if alignment.mirror_y:v=1-v
    u+=alignment.offset_u;v+=alignment.offset_v
    if not (0<=u<1 and 0<=v<1):return None
    radius=math.tan(math.radians(alignment.radius_deg))
    return u,v,fx*radius,fy*radius

def profile_dict(alignment):
    alignment.validate()
    return dict(schema_version=1,mode='MANUAL_APPROXIMATE_DIRECTION_PREVIEW',
        calibrated=False,distance_estimated=False,physical_AV_sync_verified=False,
        intrinsics_measured=False,settings=asdict(alignment))

def align_direction_to_point(azimuth_deg,elevation_deg,u,v,alignment:PreviewAlignment):
    """用静止声源的画面参照调整显示偏移；不改声学方向，不推断距离或精度。"""
    if not all(isinstance(x,(int,float)) and not isinstance(x,bool) and math.isfinite(x) and 0<=x<1 for x in (u,v)):
        raise ValueError('请选择实际视频画面内的位置')
    projected=project_direction(azimuth_deg,elevation_deg,alignment)
    if projected is None:
        raise ValueError('当前方向在设定视场外，请先调整中心方向和视场角')
    result=replace(alignment,offset_u=alignment.offset_u+u-projected[0],
                   offset_v=alignment.offset_v+v-projected[1])
    result.validate()
    return result

def load_profile(data):
    if not isinstance(data,dict) or data.get('schema_version')!=1 or data.get('mode')!='MANUAL_APPROXIMATE_DIRECTION_PREVIEW':raise ValueError('不是粗对齐预览配置')
    if any(data.get(k) is not False for k in ('calibrated','distance_estimated','physical_AV_sync_verified','intrinsics_measured')):raise ValueError('粗对齐文件不能声明已完成精确标定')
    try:profile=PreviewAlignment(**data['settings']);profile.validate();return profile
    except (TypeError,KeyError) as exc:raise ValueError('粗对齐配置字段错误') from exc
