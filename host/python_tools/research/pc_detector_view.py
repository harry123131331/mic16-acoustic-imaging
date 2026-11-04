"""Small offline research result viewer; never labels a replay as board live."""
import json
from pathlib import Path
import numpy as np


def write_result_view(folder, outputs):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.font_manager import FontProperties
    folder = Path(folder); (folder/'figures').mkdir(exist_ok=True)
    font = FontProperties(fname='C:/Windows/Fonts/msyh.ttc')
    display = []
    for number, output in enumerate(outputs):
        h = output['heatmap']; az, el = np.meshgrid(h['azimuths_deg'], h['elevations_deg'])
        values = np.asarray(h['relative_vote_density_db'])
        fig = plt.figure(figsize=(12.5,5.6))
        left = fig.add_subplot(121, projection='3d')
        left.plot_surface(az,el,values,cmap='turbo',vmin=-30,vmax=0,linewidth=0)
        left.set(xlabel='Azimuth / deg',ylabel='Elevation / deg',zlabel='Relative votes / dB',zlim=(-30,0))
        left.view_init(28,-58);left.set_title('物理观测票密度（非神经评分）',fontproperties=font)
        right = fig.add_subplot(122)
        im = right.contourf(az,el,values,levels=np.arange(-30,1,2),cmap='turbo',vmin=-30,vmax=0)
        fig.colorbar(im,ax=right,label='Relative votes / dB',fraction=.045,pad=.03)
        for source in output['sources']:
            a,e=source['azimuth_deg'],source['elevation_deg']
            right.scatter(a,e,marker='x',color='black',s=85,lw=2.5,zorder=5)
            right.annotate(f"S{source['source_id']} {source['detection_score']:.3f}",(a,e),xytext=(7,7),textcoords='offset points',fontsize=9)
        right.set(xlabel='Azimuth / deg',ylabel='Elevation / deg',xlim=(-60,60),ylim=(-30,30))
        right.grid(alpha=.25);right.set_title('二维热力图＋最终检出（黑×）',fontproperties=font)
        interval=output['audio_interval_s'];duration=output['timings_s']['processing_with_heatmap']
        fig.suptitle(f"{output['data_origin']} · 窗{number} · 检出{output['source_count']}源 · {duration:.3f}s",fontproperties=font,fontsize=13)
        fig.text(.5,.025,f'音频区间{interval[0]:.6f}—{interval[1]:.6f}s；各窗归一化票密度，不是声压或神经概率；源编号仅在本窗有效。',ha='center',fontproperties=font,fontsize=9)
        fig.subplots_adjust(top=.84,bottom=.18,left=.035,right=.95,wspace=.23)
        name=f'window_{number:03d}.png';fig.savefig(folder/'figures'/name,dpi=130);plt.close(fig)
        display.append(dict(window=number,data_origin=output['data_origin'],sources=output['sources'],
            audio_interval_s=interval,timings_s=output['timings_s'],figure='figures/'+name))
    payload=json.dumps(display,ensure_ascii=False,allow_nan=False).replace('<','\\u003c')
    html='''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>MIC16 保留模型上位机结果</title><style>
body{font:16px "Microsoft YaHei",sans-serif;background:#edf3f7;color:#142d43;margin:0}header{background:#122c44;color:white;padding:22px}main{max-width:1300px;margin:auto;padding:20px}.panel{background:white;padding:18px;margin:14px 0;border-radius:9px}select{font:inherit;padding:8px;min-width:210px}table{border-collapse:collapse;width:100%}th,td{padding:10px;text-align:left;border-bottom:1px solid #d7e0e9}img{width:100%}.muted{color:#486378}strong{color:#006e83}
</style><header><h2>MIC16 多点声源检测 · 保留模型上位机入口</h2>研究入口／回放结果，尚未认证持续实时或实板</header><main>
<div class="panel"><label>选择完整音频窗 <select id="window"></select></label><p id="status"></p><p id="time" class="muted"></p></div>
<div class="panel"><table><thead><tr><th>本窗源</th><th>方位角</th><th>俯仰角</th><th>检测评分</th></tr></thead><tbody id="sources"></tbody></table><p class="muted">仅检出空间点声源，不识别类别或说话人。0.65评分门限，15°检出去重；评分未校准为概率。</p></div>
<div class="panel"><img id="heatmap" alt="三维票密度及二维定位热力图"></div>
<div class="panel muted">热图来自物理观测票密度，非SPL；黑×来自神经评分后解码。音频需连续40960×16样本、39062.5Hz及独立8192×16静音参考。此页展示已计算结果，不会重复运行模型，也不代表实时板卡连接。</div></main>
<script>const rows=PAYLOAD; const selector=document.querySelector('#window');rows.forEach((r,i)=>{const o=document.createElement('option');o.value=i;o.textContent='窗 '+i;selector.appendChild(o)});
function show(){const r=rows[Number(selector.value)];document.querySelector('#status').textContent=r.data_origin+' | 检出 '+r.sources.length+' 个声源';document.querySelector('#time').textContent='音频 '+r.audio_interval_s.map(x=>x.toFixed(6)).join(' — ')+' s | 算法 '+r.timings_s.algorithm_total.toFixed(3)+' s | 含热图数据 '+r.timings_s.processing_with_heatmap.toFixed(3)+' s（图片保存和页面显示另计）';const body=document.querySelector('#sources');body.replaceChildren();r.sources.forEach(s=>{const tr=document.createElement('tr');['S'+s.source_id,s.azimuth_deg.toFixed(2)+'°',s.elevation_deg.toFixed(2)+'°',s.detection_score.toFixed(4)].forEach(v=>{const td=document.createElement('td');td.textContent=v;tr.appendChild(td)});body.appendChild(tr)});if(!r.sources.length){const tr=document.createElement('tr');const td=document.createElement('td');td.colSpan=4;td.textContent='本窗无检出';tr.appendChild(td);body.appendChild(tr)}document.querySelector('#heatmap').src=r.figure};selector.onchange=show;if(rows.length)show();</script></html>'''.replace('PAYLOAD',payload)
    (folder/'index.html').write_text(html,encoding='utf-8')

