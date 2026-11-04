"""固定安装、相近工作平面的人工多点对齐；不测距，不声明精密或时间标定。"""
from __future__ import annotations
from dataclasses import dataclass
import json
import math
import os
from pathlib import Path
import tempfile
import numpy as np


def bearing(azimuth,elevation):
    if not all(isinstance(x,(int,float)) and not isinstance(x,bool) and math.isfinite(x) for x in (azimuth,elevation)):
        raise ValueError('对齐方向必须为有限角度')
    if not -180<=azimuth<=180 or not -90<=elevation<=90:raise ValueError('对齐角度越界')
    az,el=np.deg2rad([azimuth,elevation])
    return np.array([np.cos(el)*np.sin(az),np.sin(el),np.cos(el)*np.cos(az)])


@dataclass(frozen=True)
class BearingImageMap:
    matrix:tuple

    def array(self):
        h=np.asarray(self.matrix,dtype=np.float64)
        if h.size!=9 or not np.isfinite(h).all():raise ValueError('多点对齐矩阵无效')
        h=h.reshape(3,3)
        if np.linalg.cond(h)>1e6:raise ValueError('多点对齐矩阵不稳定')
        return h

    def project(self,azimuth,elevation,*,allow_outside=False):
        p=self.array()@bearing(azimuth,elevation)
        if p[2]<=1e-9:return None
        u,v=p[:2]/p[2]
        if not allow_outside and not (0<=u<1 and 0<=v<1):return None
        return float(u),float(v)

    def inverse_rays(self,u,v):
        inv=np.linalg.inv(self.array())
        x=inv[0,0]*u+inv[0,1]*v+inv[0,2]
        y=inv[1,0]*u+inv[1,1]*v+inv[1,2]
        z=inv[2,0]*u+inv[2,1]*v+inv[2,2]
        return x,y,z


def _normalize(points):
    center=points.mean(axis=0);distance=np.linalg.norm(points-center,axis=1).mean()
    if distance<1e-5:raise ValueError('对齐点过于集中，请换到四个不同位置')
    scale=np.sqrt(2.)/distance
    transform=np.array([[scale,0.,-scale*center[0]],[0.,scale,-scale*center[1]],[0.,0.,1.]])
    return (points-center)*scale,transform


def fit_bearing_image_map(records):
    """归一化DLT拟合四点；必须由第五个未参与拟合的位置独立核验。"""
    if len(records)!=4:raise ValueError('需要四个不同位置的对齐点')
    vectors=np.array([bearing(*r['angles']) for r in records]);pixels=np.array([r['uv'] for r in records],dtype=float)
    if np.any(vectors[:,2]<=.1) or pixels.shape!=(4,2) or not np.isfinite(pixels).all() or np.any(pixels<0) or np.any(pixels>=1):
        raise ValueError('对齐点不在有效前方画面内')
    q=vectors[:,:2]/vectors[:,2,None];qn,tq=_normalize(q);pn,tp=_normalize(pixels)
    rows=[]
    for (x,y),(u,v) in zip(qn,pn):
        rows.extend([[-x,-y,-1.,0.,0.,0.,u*x,u*y,u],[0.,0.,0.,-x,-y,-1.,v*x,v*y,v]])
    _,s,vt=np.linalg.svd(np.asarray(rows),full_matrices=True)
    if s[-1]<s[0]*1e-7:raise ValueError('对齐点接近同一直线，请使用四角附近的位置')
    h=np.linalg.inv(tp)@vt[-1].reshape(3,3)@tq
    if abs(h[2,2])<1e-9:raise ValueError('对齐变换异常，请重新选点')
    h/=h[2,2];mapping=BearingImageMap(tuple(h.ravel()));mapping.array()
    for record in records:
        p=mapping.project(*record['angles'],allow_outside=True)
        if p is None or np.linalg.norm(np.array(p)-record['uv'])>1e-6:raise ValueError('拟合点无法正确重投影')
    return mapping


def check_holdout(mapping,record,width,height):
    if type(width) is not int or type(height) is not int or min(width,height)<=0:raise ValueError('独立核验画幅错误')
    uv=np.asarray(record['uv'],dtype=float)
    if uv.shape!=(2,) or not np.isfinite(uv).all() or np.any(uv<0) or np.any(uv>=1):raise ValueError('独立核验点必须在画面内')
    p=mapping.project(*record['angles'],allow_outside=True)
    if p is None:raise ValueError('独立核验点投影到了背面')
    error=float(np.linalg.norm((np.asarray(p)-uv)*[width,height]))
    limit=.05*min(width,height)
    return dict(passed=error<=limit,error_pixels=error,limit_pixels=limit,
                independent_point=True,used_for_fitting=False,predicted_uv=list(p))


def save_bearing_alignment(path,mapping,binding,roll,records,holdout):
    if holdout.get('passed') is not True or holdout.get('used_for_fitting') is not False or holdout.get('independent_point') is not True:
        raise ValueError('独立位置核验未通过，未保存多点参数')
    mapping.array()
    _check_records(mapping,binding,records)
    doc=dict(version=1,mode='MANUAL_WORK_PLANE_BEARING_MAP',matrix=list(mapping.matrix),
        video_binding=binding,installation_roll_deg=roll,records=records,holdout=holdout,
        calibrated=False,intrinsics_measured=False,distance_estimated=False,physical_AV_sync_verified=False,
        scope='固定安装、相近工作平面/距离；不能保证任意深度或补偿声学估计错误')
    text=json.dumps(doc,ensure_ascii=False,allow_nan=False,indent=2)+'\n'
    if len(text.encode('utf-8'))>32768:raise ValueError('多点对齐记录过大')
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True);temporary=None
    try:
        with tempfile.NamedTemporaryFile('w',encoding='utf-8',dir=path.parent,prefix=path.name+'.',suffix='.tmp',delete=False) as stream:
            temporary=Path(stream.name);stream.write(text)
        os.replace(temporary,path);temporary=None
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)


def _check_records(mapping,binding,records):
    if not isinstance(records,list) or len(records)!=5:raise ValueError('多点配置缺少独立核验')
    fit=fit_bearing_image_map(records[:4])
    if not np.allclose(fit.array(),mapping.array(),rtol=1e-9,atol=1e-10):raise ValueError('多点矩阵与对应记录不一致')
    held=bearing(*records[4]['angles'])
    if any(np.rad2deg(np.arccos(np.clip(held@bearing(*r['angles']),-1.,1.)))<4. for r in records[:4]):
        raise ValueError('独立核验必须使用不同的位置')
    validation=check_holdout(mapping,records[4],binding['width'],binding['height'])
    if validation['passed'] is not True:raise ValueError('保存的独立核验点未通过')
    return validation


def load_bearing_alignment(path,binding,roll):
    path=Path(path)
    if path.stat().st_size>32768:raise ValueError('多点对齐记录过大')
    doc=json.loads(path.read_text(encoding='utf-8'))
    if doc.get('version')!=1 or doc.get('mode')!='MANUAL_WORK_PLANE_BEARING_MAP':raise ValueError('多点配置格式错误')
    if doc.get('video_binding')!=binding or doc.get('installation_roll_deg')!=roll:raise ValueError('多点参数与当前相机/安装不匹配')
    for name in ('calibrated','intrinsics_measured','distance_estimated','physical_AV_sync_verified'):
        if doc.get(name) is not False:raise ValueError('人工多点参数不能声明完成精密或时间标定')
    records=doc.get('records');held=doc.get('holdout',{})
    if not isinstance(records,list) or len(records)!=5 or held.get('passed') is not True or held.get('used_for_fitting') is not False or held.get('independent_point') is not True:
        raise ValueError('多点配置缺少独立核验')
    stored=BearingImageMap(tuple(doc['matrix']));stored.array()
    validation=_check_records(stored,binding,records)
    return stored,validation
