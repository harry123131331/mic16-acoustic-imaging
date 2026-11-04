"""显式前半球网格合同；方位MIC12为0、MIC0为90，俯仰相对PCB平面。"""
from __future__ import annotations
import math
import numpy as np

HEMISPHERE_ALGORITHM=0xa205
HEMISPHERE_GRID='ARRAY_HEMISPHERE_25X13'
LEGACY_GRID='LEGACY_RAW_25X13'

def grid_geometry(frame):
    return HEMISPHERE_GRID if getattr(frame,'algorithm',None)==HEMISPHERE_ALGORITHM and getattr(frame,'wire_version',None)==4 else LEGACY_GRID

def raw_grid_direction(grid):
    if type(grid)is not int or not 0<=grid<325:raise ValueError('前半球网格索引错误')
    phi=math.radians((grid%25%24)*15.)
    epsilon=math.radians((grid//25)*7.5)
    x,y,z=math.cos(epsilon)*math.sin(phi),-math.cos(epsilon)*math.cos(phi),math.sin(epsilon)
    x,y,z=(0. if abs(v)<1e-14 else v for v in (x,y,z))
    return math.degrees(math.atan2(x,z)),math.degrees(math.atan2(y,math.hypot(x,z)))

def sample_hemisphere(values,x,y,z):
    """原生射线反查板面极角网格；缝两端为同一射线，背面不绘制。"""
    values=np.asarray(values)
    if values.shape!=(13,25) or not np.isfinite(values).all():raise ValueError('前半球响应错误')
    phi=np.mod(np.rad2deg(np.arctan2(x,-y)),360.)
    epsilon=np.rad2deg(np.arctan2(z,np.hypot(x,y)))
    gx=np.clip(phi/15.,0.,24.);gy=np.clip(epsilon/7.5,0.,12.)
    ix=np.minimum(gx.astype(int),23);iy=np.minimum(gy.astype(int),11)
    fx=gx-ix;fy=gy-iy
    out=(values[iy,ix]*(1-fx)+values[iy,ix+1]*fx)*(1-fy)+(values[iy+1,ix]*(1-fx)+values[iy+1,ix+1]*fx)*fy
    return out,(z>=0)
