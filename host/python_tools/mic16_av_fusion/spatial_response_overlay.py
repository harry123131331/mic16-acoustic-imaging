"""真实响应投影与方向热斑显示；低响应透明，方向标记颜色渐变不作为物理谱。"""
from __future__ import annotations
import numpy as np
from dataclasses import replace
from .preview_projection import PreviewAlignment,project_direction

POSITIONS=np.array([0.,.22,.45,.68,.85,1.])
COLORS=np.array([(0,3,46),(0,28,156),(0,144,208),(0,192,40),(246,204,0),(206,0,0)],np.float64)

def response_rgba(values,alignment:PreviewAlignment,width=320,height=180,levels=(.1,.35),*,extent_percent=100,focus_direction=None,bearing_map=None,scan_geometry='LEGACY_RAW_25X13'):
    alignment.validate()
    x=np.asarray(values,dtype=np.float64)
    if x.shape!=(13,25) or not np.isfinite(x).all() or np.max(np.abs(x))>2.000005:raise ValueError('空间响应网格错误')
    if not (isinstance(width,int) and isinstance(height,int) and 1<=width<=1280 and 1<=height<=720):raise ValueError('叠加图尺寸错误')
    low,high=map(float,levels)
    if not np.isfinite([low,high]).all() or high<=low:raise ValueError('显示色限错误')
    if type(extent_percent) is not int or not 25<=extent_percent<=100:raise ValueError('热斑显示范围错误')
    # 逐像素反投影到阵列方向，使用与project_direction完全相反的旋转次序。
    u,v=np.meshgrid((np.arange(width)+.5)/width,(np.arange(height)+.5)/height)
    u-=alignment.offset_u;v-=alignment.offset_v
    if alignment.mirror_x:u=1-u
    if alignment.mirror_y:v=1-v
    yaw,pitch,hf,vf=np.deg2rad([alignment.yaw_deg,alignment.pitch_deg,alignment.horizontal_fov_deg,alignment.vertical_fov_deg])
    cx=(u-.5)*2*np.tan(hf/2);cy=(.5-v)*2*np.tan(vf/2)
    if alignment.roll_deg:
        # 先撤销安装滚转，再撤销俯仰/偏航，与方向点的正投影严格配对。
        roll=np.deg2rad(alignment.roll_deg)
        cx,cy=np.cos(roll)*cx+np.sin(roll)*cy,-np.sin(roll)*cx+np.cos(roll)*cy
    y=np.cos(pitch)*cy+np.sin(pitch);z=-np.sin(pitch)*cy+np.cos(pitch)
    wx=np.cos(yaw)*cx+np.sin(yaw)*z;wz=-np.sin(yaw)*cx+np.cos(yaw)*z
    if bearing_map is not None:
        # 多点关系直接从原视频反投影至原始声学射线，禁止再叠加旧偏角/镜像。
        screen_u,screen_v=np.meshgrid((np.arange(width)+.5)/width,(np.arange(height)+.5)/height)
        wx,y,wz=bearing_map.inverse_rays(screen_u,screen_v)
    az=np.rad2deg(np.arctan2(wx,wz));el=np.rad2deg(np.arctan2(y,np.hypot(wx,wz)))
    from .hemisphere_grid import HEMISPHERE_GRID,LEGACY_GRID,sample_hemisphere
    if scan_geometry==HEMISPHERE_GRID:
        value,inside=sample_hemisphere(x,wx,y,wz)
    elif scan_geometry==LEGACY_GRID:
        inside=(az>=-60)&(az<=60)&(el>=-30)&(el<=30)
        if bearing_map is not None:inside&=wz>0
        gx=np.clip((az+60)/5,0,24);gy=np.clip((el+30)/5,0,12)
        ix=np.minimum(gx.astype(int),23);iy=np.minimum(gy.astype(int),11);fx=gx-ix;fy=gy-iy
        value=(x[iy,ix]*(1-fx)+x[iy,ix+1]*fx)*(1-fy)+(x[iy+1,ix]*(1-fx)+x[iy+1,ix+1]*fx)*fy
    else:raise ValueError('未知空间响应网格')
    t=np.clip((value-low)/(high-low),0,1)
    rgba=np.empty((height,width,4),np.uint8)
    for channel in range(3):rgba[:,:,channel]=np.rint(np.interp(t,POSITIONS,COLORS[:,channel])).astype(np.uint8)
    # 蓝/青背景透明，较高真实响应逐渐显色；色限固定，不逐帧增强或归一化。
    # 缩小范围只隐藏较弱的真实响应，不缩放坐标、挪动峰值或生成新斑点。
    # 不裁剪方向时，各档仅影响透明度；裁剪方向时另加明确的显示颜色渐变。
    cutoff=.56+.44*(1.-extent_percent/100.)
    transition=.44*(extent_percent/100.)
    rgba[:,:,3]=np.rint(np.clip((t-cutoff)/transition,0,1)*220*inside).astype(np.uint8)
    if focus_direction is not None:
        # 相机页只展示有效方向附近的一小块真实响应。裁剪是显示范围，
        # 不是主瓣宽度或精度；不得把饱和平台改造成更尖锐的物理谱。
        if not isinstance(focus_direction,(tuple,list)) or len(focus_direction)!=2:raise ValueError('热斑显示方向错误')
        bounded=replace(alignment,radius_deg=max(.1,alignment.radius_deg*extent_percent/100.))
        point=(project_direction(*focus_direction,bounded) if bearing_map is None else bearing_map.project(*focus_direction))
        if point is None:rgba[:,:,3]=0
        else:
            if bearing_map is None:
                center_u,center_v,radius_u,radius_v=point
                columns=(np.arange(width)+.5)/width;rows=(np.arange(height)+.5)/height
                distance=np.hypot((columns[None,:]-center_u)/radius_u,(rows[:,None]-center_v)/radius_v)
            else:
                from .bearing_image_alignment import bearing
                direction=bearing(*focus_direction);length=np.sqrt(wx*wx+y*y+wz*wz)
                cosine=np.clip((wx*direction[0]+y*direction[1]+wz*direction[2])/length,-1.,1.)
                distance=np.arccos(cosine)/np.deg2rad(bounded.radius_deg)
            edge=np.clip((1.-distance)/.18,0,1)
            # 用户指定相机标记红到蓝渐变。颜色使用独立显示窗，原响应/质量门不变；
            # 这个径向渐变是方向标记的显示样式，不是主定位页的物理响应测量。
            # 方向标记固定红心到蓝边；真实响应只控制是否可见，不再把红心降成绿/青。
            display_value=np.clip((1.-distance)/.65,0,1)
            for channel in range(3):
                rgba[:,:,channel]=np.rint(np.interp(display_value,POSITIONS,COLORS[:,channel])).astype(np.uint8)
            rgba[:,:,3]=np.rint(rgba[:,:,3]*edge).astype(np.uint8)
    return rgba
