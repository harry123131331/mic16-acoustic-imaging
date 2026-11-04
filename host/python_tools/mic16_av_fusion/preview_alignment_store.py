"""将人工对齐参数绑定实际视频几何，避免换相机或画幅后沿用错误配置。"""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import tempfile

from .preview_projection import load_profile,profile_dict


def _plain(value):
    if isinstance(value,(tuple,list)):
        if len(value)>16:raise ValueError('视频几何字段过长')
        return [_plain(x) for x in value]
    if type(value) in (str,int,bool):
        if isinstance(value,str) and len(value)>4096:raise ValueError('相机标识过长')
        return value
    if type(value) is float and math.isfinite(value):return value
    raise ValueError('视频几何字段无效')


def video_binding(meta):
    """帧号与会话不属于安装几何；设备、画幅、模式及旋转镜像必须完全匹配。"""
    width,height=meta.width,meta.height
    if not all(type(v) is int and 1<=v<=16384 for v in (width,height)):
        raise ValueError('视频画幅无效')
    return dict(width=width,height=height,mount_id=_plain(meta.mount_id),
                mode_id=_plain(meta.mode_id),transform_id=_plain(meta.transform_id))


def save_bound_alignment(path,alignment,binding,reference=None):
    """小文件原子替换；记录人工参照，但始终保留未精确标定、未核验同步声明。"""
    document=profile_dict(alignment)
    document['video_binding']=binding
    document['reference_observation']=reference
    text=json.dumps(document,ensure_ascii=False,allow_nan=False,indent=2)+'\n'
    if len(text.encode('utf-8'))>32768:raise ValueError('声像对齐配置过大')
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(mode='w',encoding='utf-8',dir=path.parent,
                                          prefix=path.name+'.',suffix='.tmp',delete=False) as stream:
            temporary=Path(stream.name);stream.write(text)
        os.replace(temporary,path);temporary=None
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)


def load_bound_alignment(path,binding):
    path=Path(path)
    if path.stat().st_size>32768:raise ValueError('声像对齐配置过大')
    document=json.loads(path.read_text(encoding='utf-8'))
    alignment=load_profile(document)
    if document.get('video_binding')!=binding:
        raise ValueError('已保存参数与当前相机、画幅或旋转镜像不匹配')
    reference=document.get('reference_observation')
    if reference is not None and not isinstance(reference,dict):
        raise ValueError('声像对齐参照记录无效')
    return alignment,reference
