"""阵列平面球坐标与方向投影；保留原始扫描方向，正面MIC12为0度、MIC0为90度。"""
from __future__ import annotations
import math
from .installation_coordinates import _roll


def array_direction_vector(raw_azimuth_deg,raw_elevation_deg,roll_deg=90.):
    roll=_roll(roll_deg)
    if not all(isinstance(v,(int,float)) and not isinstance(v,bool) and math.isfinite(v)
               for v in (raw_azimuth_deg,raw_elevation_deg)):
        raise ValueError('原始方向必须为有限角度')
    if not -180<=raw_azimuth_deg<=180 or not -90<=raw_elevation_deg<=90:raise ValueError('原始方向越界')
    az,el=map(math.radians,(raw_azimuth_deg,raw_elevation_deg))
    x,y,z=math.cos(el)*math.sin(az),math.sin(el),math.cos(el)*math.cos(az)
    return math.cos(roll)*x-math.sin(roll)*y,math.sin(roll)*x+math.cos(roll)*y,z


def array_spherical_angles(raw_azimuth_deg,raw_elevation_deg,roll_deg=90.):
    """方位0～360；俯仰相对板面，正前方90度。法线方向的方位未定义。"""
    x,y,z=array_direction_vector(raw_azimuth_deg,raw_elevation_deg,roll_deg)
    radius=math.hypot(x,y)
    azimuth=None if radius<=1e-12 else math.degrees(math.atan2(y,x))%360.
    if azimuth is not None and (abs(azimuth)<1e-10 or abs(azimuth-360.)<1e-10):azimuth=0.
    elevation=math.degrees(math.atan2(z,radius))
    return azimuth,0. if abs(elevation)<1e-10 else elevation


def array_projection(azimuth_deg,elevation_deg):
    if not isinstance(elevation_deg,(int,float)) or isinstance(elevation_deg,bool) or not math.isfinite(elevation_deg) or not -90<=elevation_deg<=90:
        raise ValueError('板面俯仰角错误')
    radius=math.cos(math.radians(elevation_deg))
    if abs(radius)<=1e-12:return 0.,0.
    if not isinstance(azimuth_deg,(int,float)) or isinstance(azimuth_deg,bool) or not math.isfinite(azimuth_deg):
        raise ValueError('非法或未定义的方位角')
    az=math.radians(azimuth_deg)
    return radius*math.cos(az),radius*math.sin(az)


def display_view_angles(azimuth_deg,elevation_deg):
    """同一方向换成矩形图的左右/上下偏角；不把它冒充板面球坐标。"""
    x,y=array_projection(azimuth_deg,elevation_deg);z=math.sin(math.radians(elevation_deg))
    return math.degrees(math.atan2(x,z)),math.degrees(math.atan2(y,math.hypot(x,z)))
