"""真实响应的显示插值：先插值标量再着色，避免红与蓝直接混成紫色。"""
from __future__ import annotations
import numpy as np


def response_display_levels(values,manual=(.1,.35),*,automatic=False):
    """自动模式只提高色标上限，原始响应不归一化，色条保留实际响应单位。"""
    low,high=map(float,manual)
    if not np.isfinite([low,high]).all() or low>=high:raise ValueError('响应显示色限错误')
    if not automatic:return low,high
    a=np.asarray(values);finite=a[np.isfinite(a)]
    return low,max(high,float(finite.max())) if finite.size else high


def scalar_display_grid(values,factor=9):
    """同一图域内的双线性插值；保留未知区域，不外推，不改变声学网格。"""
    a=np.asarray(values)
    if a.ndim!=2 or min(a.shape)<2 or max(a.shape)>128 or np.isinf(a).any():
        raise ValueError('空间响应显示网格错误')
    if type(factor)is not int or not 1<=factor<=17:raise ValueError('显示插值倍率错误')
    if factor==1:return a.copy()
    rows,cols=a.shape
    # 像素边界与原图一致；奇数倍率保留每个原始采样点的精确像素中心。
    x=np.clip((np.arange(cols*factor)+.5)/factor-.5,0,cols-1)
    y=np.clip((np.arange(rows*factor)+.5)/factor-.5,0,rows-1)
    ix=np.minimum(x.astype(int),cols-2);iy=np.minimum(y.astype(int),rows-2)
    fx=(x-ix)[None,:];fy=(y-iy)[:,None]
    corners=[a[iy[:,None],ix[None,:]],a[iy[:,None],ix[None,:]+1],
             a[iy[:,None]+1,ix[None,:]],a[iy[:,None]+1,ix[None,:]+1]]
    weights=[(1-fx)*(1-fy),fx*(1-fy),(1-fx)*fy,fx*fy]
    result=np.zeros((rows*factor,cols*factor),np.float64);known=np.ones(result.shape,bool)
    for value,weight in zip(corners,weights):
        finite=np.isfinite(value);known&=finite|(weight<=1e-14)
        result+=np.where(finite,value,0.)*weight
    result[~known]=np.nan
    return result
