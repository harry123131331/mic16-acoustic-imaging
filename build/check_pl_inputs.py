"""检查提交包当前PL输入是否齐备；只读文件，不启动AMD工具或板卡。"""
from pathlib import Path
import hashlib,json,re,sys
from xml.etree import ElementTree as E
ROOT=Path(__file__).resolve().parents[1]
NS={'s':'http://www.spiritconsortium.org/XMLSchema/SPIRIT/1685-2009'}
EXPECTED={'shared_spectrum':'mic16:acoustic:m16_sp_shared:2.0',
          'shared_compute':'mic16:acoustic:m16_shared_projection_engine:6.0',
          'scanner':'xilinx.com:hls:mic16_tf_zone_streaming_hls_q2_24:1.0'}
def main():
    errors=[];catalogs=[]
    for name,vlnv in EXPECTED.items():
        repo=ROOT/'fpga/ip/release'/name
        component=repo/'component.xml'
        if not component.is_file():errors.append(str(component.relative_to(ROOT)));continue
        x=E.parse(component).getroot()
        actual=':'.join(x.find('s:'+k,NS).text for k in ['vendor','library','name','version'])
        if actual!=vlnv:errors.append(name+':VLNV不一致')
        files=sorted(set(n.text for n in x.findall('.//s:file/s:name',NS)))
        for relative in files:
            p=repo/relative
            if not p.resolve().is_relative_to(repo.resolve()) or not p.is_file():errors.append(name+'/'+relative)
            if relative!=relative.lower():errors.append(name+':引用路径不是小写:'+relative)
        catalogs.append(dict(name=name,vlnv=actual,declared_files=len(files)))
    required=['build/rebuild_pl.tcl','build/release_bd.tcl',
       'fpga/constraints/ac880_audio_dataflow_pins.xdc','fpga/constraints/ac880_audio_dataflow_timing.tcl']
    required+=['fpga/rtl/audio/'+x+'.v' for x in ['mic16_cic_compensator','pdm_axis_pack_nch','pdm_clk_gen','pdm_decimator','pdm_decimator_shared','pl_pdm_top_16']]
    for relative in required:
        if not (ROOT/relative).is_file():errors.append(relative)
    bd=(ROOT/'build/release_bd.tcl').read_text(encoding='utf-8')
    if 'set run_remote_bd_flow 0' not in bd:errors.append('BD未切换本地相对路径模式')
    for vlnv in EXPECTED.values():
        if vlnv not in bd:errors.append('BD缺少'+vlnv)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    origin=json.loads((ROOT/'build/release_input_origins.json').read_text(encoding='utf-8'))
    for relative,meta in origin['files'].items():
        p=ROOT/relative
        if not p.is_file() or sha(p)!=meta['submission_sha256']:errors.append('发布输入散列不一致:'+relative)
    result=dict(passed=not errors,catalogs=catalogs,required_design_files=len(required),
       release_files=len(origin['files']),errors=errors,Vivado_executed=False,HLS_executed=False,board_operations=0,
       claim='仅文件依赖和版本检查；不是Vivado展开、综合、实现或新BIT的验证结果')
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0 if result['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
