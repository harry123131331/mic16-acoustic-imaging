"""通用流处理证据检查：区分输入、分析覆盖、结果刷新与物理端到端测量。"""
from pathlib import Path
import argparse,json,math,unittest

def audit(d):
    errors=[]
    for key in ['sample_rate_hz','samples_per_frame']:
        v=d.get(key)
        if not isinstance(v,(int,float)) or isinstance(v,bool) or not math.isfinite(v) or v<=0:errors.append(key+'必须为正有限数')
    if errors:return errors
    for key in ['captured_frames','analyzed_frames','uncovered_frames']:
        v=d.get(key)
        if not isinstance(v,int) or isinstance(v,bool) or v<0:errors.append(key+'必须为非负整数')
    if errors:return errors
    if d['analyzed_frames']+d['uncovered_frames']!=d['captured_frames']:errors.append('分析与未分析覆盖之和必须等于采集帧数')
    records=d.get('results',[]);last=-1
    for r in records:
        seq=r.get('sequence');start=r.get('first_sample');end=r.get('end_sample')
        if not all(isinstance(x,int) and not isinstance(x,bool) for x in [seq,start,end]) or seq<=last or start<0 or end<=start:
            errors.append('结果序号或样本区间无效');continue
        last=seq
        if r.get('received_time_ms') is not None and r.get('painted_time_ms') is not None:
            if r['painted_time_ms']<r['received_time_ms']:errors.append('同一电脑时钟下绘制时间不能早于收包')
    if d.get('claims_physical_latency') and not d.get('physical_event_reference'):errors.append('物理延迟主张缺少实际事件参照，接收年龄不能替代')
    if d.get('claims_every_frame_analyzed') and d['uncovered_frames']:errors.append('存在未分析帧，不能声称全帧分析')
    return errors

class Checks(unittest.TestCase):
    def base(self):return dict(sample_rate_hz=1000,samples_per_frame=100,captured_frames=10,analyzed_frames=4,uncovered_frames=6,results=[dict(sequence=1,first_sample=0,end_sample=400,received_time_ms=10,painted_time_ms=20)])
    def test_valid(self):self.assertEqual(audit(self.base()),[])
    def test_false_physical_latency(self):
        d=self.base();d['claims_physical_latency']=True;self.assertTrue(audit(d))
    def test_false_full_coverage(self):
        d=self.base();d['claims_every_frame_analyzed']=True;self.assertTrue(audit(d))
    def test_bad_sample_axis(self):
        d=self.base();d['results'][0]['end_sample']=-1;self.assertTrue(audit(d))

def main():
    ap=argparse.ArgumentParser(description=__doc__);g=ap.add_mutually_exclusive_group(required=True)
    g.add_argument('--input',type=Path);g.add_argument('--self-test',action='store_true');args=ap.parse_args()
    if args.self_test:
        result=unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Checks));raise SystemExit(not result.wasSuccessful())
    d=json.loads(args.input.read_text(encoding='utf-8'));errors=audit(d)
    print(json.dumps(dict(passed=not errors,errors=errors,input_period_ms=1000*d['samples_per_frame']/d['sample_rate_hz'],board_accessed=False),ensure_ascii=False))
    raise SystemExit(bool(errors))
if __name__=='__main__':main()
