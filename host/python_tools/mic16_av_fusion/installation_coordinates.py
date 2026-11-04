"""阵列安装旋转后的坐标显示；不改声学方向、原始空间响应或相机标定。"""
from __future__ import annotations
import math
import numpy as np


def _roll(value):
    if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value) or not -180<=value<=180:
        raise ValueError('阵列安装旋转错误')
    return math.radians(value)


def installed_angles(azimuth_deg,elevation_deg,roll_deg):
    """在朝前为正Z的安装坐标中返回右正、上正的方位/俯仰；不是相机像素角。"""
    roll=_roll(roll_deg)
    if not all(isinstance(v,(int,float)) and not isinstance(v,bool) and math.isfinite(v) for v in (azimuth_deg,elevation_deg)):
        raise ValueError('方向角错误')
    if not -180<=azimuth_deg<=180 or not -90<=elevation_deg<=90:raise ValueError('方向角越界')
    if roll_deg==0:return azimuth_deg,elevation_deg
    az,el=map(math.radians,(azimuth_deg,elevation_deg))
    x,y,z=math.cos(el)*math.sin(az),math.sin(el),math.cos(el)*math.cos(az)
    x,y=math.cos(roll)*x-math.sin(roll)*y,math.sin(roll)*x+math.cos(roll)*y
    azimuth=math.degrees(math.atan2(x,z));elevation=math.degrees(math.atan2(y,math.hypot(x,z)))
    return (0. if abs(azimuth)<1e-10 else azimuth,0. if abs(elevation)<1e-10 else elevation)


def installed_response_grid(values,roll_deg):
    """重投影真实响应到安装后角度轴；网格外透明，插值不增加声学分辨率。"""
    roll=_roll(roll_deg);values=np.asarray(values)
    if values.shape!=(13,25) or not np.isfinite(values).all():raise ValueError('原始空间网格错误')
    if roll_deg==0:return values,(-62.5,-32.5,125.,65.),(-60.,60.,-30.,30.)
    # 原网格与正Z的最大夹角约64.34度，绕Z转动后仍落在±65度图域。
    axis=np.arange(-65.,65.001,2.5);az,el=np.meshgrid(np.deg2rad(axis),np.deg2rad(axis))
    x,y,z=np.cos(el)*np.sin(az),np.sin(el),np.cos(el)*np.cos(az)
    x,y=np.cos(roll)*x+np.sin(roll)*y,-np.sin(roll)*x+np.cos(roll)*y
    raw_az=np.rad2deg(np.arctan2(x,z));raw_el=np.rad2deg(np.arctan2(y,np.hypot(x,z)))
    inside=(raw_az>=-60.-1e-9)&(raw_az<=60.+1e-9)&(raw_el>=-30.-1e-9)&(raw_el<=30.+1e-9)
    gx=np.clip((raw_az+60.)/5.,0.,24.);gy=np.clip((raw_el+30.)/5.,0.,12.)
    ix=np.minimum(gx.astype(int),23);iy=np.minimum(gy.astype(int),11);fx=gx-ix;fy=gy-iy
    out=(values[iy,ix]*(1.-fx)+values[iy,ix+1]*fx)*(1.-fy)+(values[iy+1,ix]*(1.-fx)+values[iy+1,ix+1]*fx)*fy
    out=np.where(inside,out,np.nan)
    return out,(-66.25,-66.25,132.5,132.5),(-65.,65.,-65.,65.)
