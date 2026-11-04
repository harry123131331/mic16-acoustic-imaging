"""阵列安装与角度约定独立于相机；当前用板面球坐标，保留历史水平约定读取。"""
from __future__ import annotations
from dataclasses import dataclass
import json
from pathlib import Path

REFERENCE_PLANE='HORIZONTAL_THROUGH_ARRAY_CENTER'
ARRAY_REFERENCE_PLANE='MICROPHONE_ARRAY_PLANE'
GEOMETRY_SHA256='6222b47c9b3a6db1742600114c5de82a87380e20d29716d806853d947229d382'


@dataclass(frozen=True)
class VerticalArrayInstallation:
    up_microphone:int=0
    user_confirmed:bool=False
    reference_plane:str=ARRAY_REFERENCE_PLANE
    pose_measured:bool=False

    @property
    def roll_deg(self):
        return float((90-self.up_microphone*22.5+180)%360-180)

    @property
    def cardinal_microphones(self):
        return dict(up=self.up_microphone,left=(self.up_microphone+4)%16,
                    down=(self.up_microphone+8)%16,right=(self.up_microphone+12)%16)

    @property
    def coordinate_frame(self):
        if self.reference_plane==ARRAY_REFERENCE_PLANE:return 'ARRAY_SPHERICAL_MIC12_ZERO' if self.user_confirmed else 'ARRAY_SPHERICAL_INSTALLATION_UNCONFIRMED'
        return 'HORIZONTAL_USER_CONFIRMED_VERTICAL_MOUNT' if self.user_confirmed else 'UNCONFIRMED_VERTICAL_MOUNT'


def vertical_installation_document(*,up_microphone=0,user_confirmed=False):
    if type(up_microphone) is not int or up_microphone not in (0,4,8,12):raise ValueError('正交安装的上方麦克风编号无效')
    if type(user_confirmed) is not bool:raise ValueError('安装确认状态无效')
    installation=VerticalArrayInstallation(up_microphone,user_confirmed,REFERENCE_PLANE)
    return dict(version=1,reference_plane=REFERENCE_PLANE,origin='MICROPHONE_ARRAY_CENTER',
        pose='VERTICAL_ARRAY_NORMAL_HORIZONTAL',cardinal_microphones=installation.cardinal_microphones,
        user_confirmed=user_confirmed,pose_measured=False,geometry_sha256=GEOMETRY_SHA256,
        description='现实水平面为0度，上正下负；阵列竖直且法线水平，正面观察编号；独立于相机参数。')


def load_vertical_installation(path):
    path=Path(path)
    if path.stat().st_size>8192:raise ValueError('阵列安装文件过大')
    doc=json.loads(path.read_text(encoding='utf-8'))
    reference=doc.get('reference_plane');version=doc.get('version')
    if ((version,reference) not in ((1,REFERENCE_PLANE),(2,ARRAY_REFERENCE_PLANE)) or
        doc.get('origin')!='MICROPHONE_ARRAY_CENTER' or doc.get('pose')!='VERTICAL_ARRAY_NORMAL_HORIZONTAL' or
        doc.get('geometry_sha256')!=GEOMETRY_SHA256):raise ValueError('阵列安装的角度基准或几何不匹配')
    if doc.get('pose_measured') is not False or type(doc.get('user_confirmed')) is not bool:
        raise ValueError('人工安装确认不能声明完成姿态精测')
    cardinal=doc.get('cardinal_microphones')
    if not isinstance(cardinal,dict) or set(cardinal)!=set(('up','left','down','right')):
        raise ValueError('阵列安装缺少上下左右编号')
    up=cardinal.get('up')
    if type(up) is not int or up not in (0,4,8,12) or any(type(v) is not int for v in cardinal.values()):
        raise ValueError('阵列安装麦克风编号无效')
    installation=VerticalArrayInstallation(up,doc['user_confirmed'],reference)
    if cardinal!=installation.cardinal_microphones:raise ValueError('阵列安装四个编号不一致')
    if version==2 and (up!=0 or doc.get('azimuth_zero_microphone')!=12 or doc.get('azimuth_quarter_microphone')!=0 or doc.get('azimuth_direction')!='MIC12_TO_MIC0_TO_MIC4_TO_MIC8'):
        raise ValueError('阵列平面方位角基准与用户确认不匹配')
    return installation
