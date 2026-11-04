"""只用内存合成记录测试验收拒绝条件；不模拟实测成绩、不连接板卡。"""
from pathlib import Path
import contextlib,copy,importlib,io,json,struct,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
analysis=importlib.import_module('analyze_statistics_board')
runner=importlib.import_module('run_statistics_board')
manifest=json.loads((ROOT/'build/board_ready/statistics_batch_20261024/manifest.json').read_text(encoding='utf-8'))

def fixture(bad_work=False,nan_migration=False):
    """极小虚构tick仅用于协议校验；没有任何性能含义，禁止发布成ARM结果。"""
    m=manifest;files={n:bytearray(r['bytes']) for n,r in m['reads'].items()}
    for n,values in m['contracts'].items():struct.pack_into('<'+'I'*len(values),files[n],0,*values)
    struct.pack_into('<6IQ',files['feature_fast_report.bin'],0,0x4d464153,0x20261022,3,20,0,0,m['timer_hz'])
    struct.pack_into('<6I',files['simd_report.bin'],0,0x4d53494d,0x20261022,20,0x80000010,0x00f00000,0)
    struct.pack_into('<4IiI',files['statistics_report.bin'],0,0x4d54494c,0x20261024,3,20,0,0)
    struct.pack_into('<7I',files['golden_report.bin'],0,76490,0,20,1,0,0,0)
    struct.pack_into('<IIi',files['scanner_report.bin'],0,21,0,0)
    text=['MIC16 FEATURE FAST EXACT_COMPUTE_SAME_INPUT',
          'FAST_CONTRACT timer_hz='+str(m['timer_hz'])+' expected_checks=76490 checkpoint='+m['checkpoint'],
          'SIMD_ENV fpscr=80000010 cpacr=00f00000 preserve_public=1']
    keys=[(r,f,mode) for r in range(2) for f in range(2) for mode in (range(5) if r==0 else range(4,-1,-1))]
    checks=0
    for idx,(rep,case,mode) in enumerate(keys):
        n=m['fixture_candidates'][case];q=m['fixture_qualified'][case];total=2000;stage=[10,20,30,5,1000,15]
        prefix=f'rep={rep} case={case} mode={mode}'
        text += ['FAST_START '+prefix,'FFT_COUNTS front=0 mono=0 stats=0','BEAM_TILES calls=0 blocks4=0 original=0',
                 'REAL_COUNTS fft4096=0 fft512=0 quiet_skipped=0 MAGNITUDE fast=0 fallback=0',
                 'DATAFLOW_COUNTS cold=0 hit=0 batches=0 reused=0 fallback=0']
        sim=[rep,case,mode,0,0,0,0,int(mode>=2),0,0,0,0]
        text.append('SIMD_COUNTS '+' '.join(k+'='+str(v) for k,v in zip(('rep','case','mode','fft4096','fft512','table','fallback','vector_groups','fir','blocks4','boundary','original'),sim)))
        struct.pack_into('<12I',files['simd_report.bin'],24+idx*48,*sim)
        migration=[0.]*5
        if nan_migration and mode==4 and case==1:migration[0]=float('nan')
        if mode>=2:text.append('SIMD_MIGRATION '+prefix+' '+' '.join(k+'='+format(v,'.6g') for k,v in zip(('direction','logit','score','probes','features'),migration))+' gates=0 STRICT_ORIGINAL_GOLDEN_FOLLOWS')
        if mode>=3:shared=[n,n,0,0,2*n,0,10,20,5,40]
        elif mode:shared=[n,0,n,0,0,0,0,0,0,0]
        else:shared=[0]*10
        struct.pack_into('<6I4Q',files['shared_report.bin'],idx*56,*shared)
        values=[shared[0],shared[1],shared[3],shared[4],shared[5],*shared[6:]]
        text.append('SHARED_COUNTS '+prefix+' '+' '.join(k+'='+str(v) for k,v in zip(('calls','complete','cpu_range','batches','errors','cache','device','invalidate','total'),values)))
        if mode==4 and n>1:
            groups=(n+3)//4;nb=17*32*groups-int(bad_work);batch=[1,67*n,67*256*groups,nb,0];device=[100,200,50,360,nb,4,0,0]
        else:batch=[0]*5;device=[0]*8
        so=24+144*idx;struct.pack_into('<4I',files['statistics_report.bin'],so,rep,case,mode,n)
        struct.pack_into('<5I',files['statistics_report.bin'],so+16,*batch)
        struct.pack_into('<4Q3Ii',files['statistics_report.bin'],so+40,*device)
        struct.pack_into('<5d',files['statistics_report.bin'],so+104,*migration)
        if mode==4:
            vals=[batch[3],batch[2],batch[1],*device[:4],batch[4]]
            text.append('TILE_COUNTS '+prefix+' '+' '.join(k+'='+str(v) for k,v in zip(('batches','groups','rows','cache','device','invalidate','total','errors'),vals)))
        av=[n,*batch,device[4],device[5],device[6],device[7],0,0,0]
        text.append('TILE_AUDIT '+prefix+' '+' '.join(k+'='+str(v) for k,v in zip(('candidates','calls','rows','groups','batches','backend_errors','device_batches','control','quarantined','kernel_return','indices','finite','migration'),av)))
        checks+=3839 if case==0 else 3810
        text.append(f'D3F_CASE case={case} candidates={n} qualified={q} checks={checks} errors=0')
        mono=0 if mode>=3 else n if mode else 2*n
        text.append('FAST_PHASE '+prefix+f' total_ticks={total} feature_ticks=1000 differences=0 beam_fast={2*n} beam_fallback=0 mono_fast={mono}')
        off=32+120*idx;struct.pack_into('<4IQ',files['feature_fast_report.bin'],off,rep,case,mode,0,total)
        struct.pack_into('<7Q4Ii',files['feature_fast_report.bin'],off+24,*stage,2000,12,63,63,1,0)
        struct.pack_into('<3I',files['feature_fast_report.bin'],off+104,2*n,0,mono)
    text.append('FAST_BENCHMARK_PASS cases=20 checks=76490 errors=0 NOT_LIVE_OR_REALTIME')
    regs={int(a):v for a,v in m['clock_registers'].items()}
    regs.update({0x80000000:4,0x80000010:0,0x80000018:0x200040,0x80020000:4,0x80020010:0,0x80020018:0x300040,
                 0x80020024:2304,0x8002002c:0,0x80020034:1024,0x8002003c:2048,0x80020044:3,0x8002004c:8,0x80020054:4})
    read='\n'.join(f'{k:08X}:   {v:08X}' for k,v in regs.items())+'\nCortex-A9 MPCore #0 (Suspended)\nCortex-A9 MPCore #1 (Suspended)\nSTATISTICS_REPORT_READ_AND_BOTH_CORES_STOPPED\n'
    return files,'\n'.join(text)+'\n',read

class Protocol(unittest.TestCase):
    def parse(self,f=None,u=None,r=None):
        files,uart,read=fixture();return analysis.parse_bundle(manifest,files if f is None else f,uart if u is None else u,read if r is None else r)
    def test_consistent_fixture_has_no_real_measurement_claim(self):
        result=self.parse();self.assertEqual(len(result['runs']),20);self.assertFalse(result['target_2s_passed']);self.assertNotIn('REAL_ARM',result['status'])
    def test_truncated_uart(self):
        _,u,_=fixture()
        with self.assertRaises(ValueError):self.parse(u=u.rsplit('FAST_BENCHMARK_PASS',1)[0])
    def test_restarted_uart(self):
        _,u,_=fixture()
        with self.assertRaises(ValueError):self.parse(u='MIC16 FEATURE FAST EXACT_COMPUTE_SAME_INPUT\n'+u)
    def test_missing_audit(self):
        _,u,_=fixture();line=next(l for l in u.splitlines() if l.startswith('TILE_AUDIT'))
        with self.assertRaises(ValueError):self.parse(u=u.replace(line+'\n','',1))
    def test_duplicate_audit(self):
        _,u,_=fixture();line=next(l for l in u.splitlines() if l.startswith('TILE_AUDIT'))
        with self.assertRaises(ValueError):self.parse(u=u.replace(line,line+'\n'+line,1))
    def test_both_logs_agree_but_work_is_missing(self):
        f,u,r=fixture(bad_work=True)
        with self.assertRaises(ValueError):analysis.parse_bundle(manifest,f,u,r)
    def test_nan_hidden_by_error_maximum(self):
        f,u,r=fixture(nan_migration=True)
        with self.assertRaises(ValueError):analysis.parse_bundle(manifest,f,u,r)
    def test_bad_clock_register(self):
        _,_,r=fixture();key=next(iter(manifest['clock_registers']));old=f'{int(key):08X}:   {manifest["clock_registers"][key]:08X}'
        with self.assertRaises(ValueError):self.parse(r=r.replace(old,f'{int(key):08X}:   00000000'))
    def test_bad_timer_frequency(self):
        _,u,_=fixture()
        with self.assertRaises(ValueError):self.parse(u=u.replace('timer_hz='+str(manifest['timer_hz']),'timer_hz=1'))
    def test_wrong_checkpoint(self):
        _,u,_=fixture()
        with self.assertRaises(ValueError):self.parse(u=u.replace(manifest['checkpoint'],'0'*64))
    def test_tail_csr_wrong(self):
        _,_,r=fixture()
        with self.assertRaises(ValueError):self.parse(r=r.replace('80020044:   00000003','80020044:   00000004'))
    def test_core_not_stopped(self):
        _,_,r=fixture()
        with self.assertRaises(ValueError):self.parse(r=r.replace('#1 (Suspended)','#1 (Running)'))
    def test_ddr_length(self):
        f,_,_=fixture();f['statistics_report.bin']=f['statistics_report.bin'][:-4]
        with self.assertRaises(ValueError):self.parse(f=f)
    def test_contract(self):
        f,_,_=fixture();struct.pack_into('<I',f['statistics_contract.bin'],0,1)
        with self.assertRaises(ValueError):self.parse(f=f)
    def test_phase_missing(self):
        f,_,_=fixture();struct.pack_into('<I',f['feature_fast_report.bin'],32+24+60,31)
        with self.assertRaises(ValueError):self.parse(f=f)
    def test_indices_changed(self):
        f,_,_=fixture();struct.pack_into('<I',f['statistics_report.bin'],24+88,1)
        with self.assertRaises(ValueError):self.parse(f=f)
    def test_uart_ddr_tick_mismatch(self):
        _,u,_=fixture()
        with self.assertRaises(ValueError):self.parse(u=u.replace('total_ticks=2000','total_ticks=2001',1))
    def test_hash_tamper(self):
        m=copy.deepcopy(manifest);name=next(iter(m['artifacts']));m['artifacts'][name]['sha256']='0'*64
        with self.assertRaises(ValueError):runner.verify_manifest(m)
    def test_default_is_offline(self):
        with patch.object(sys,'argv',['run_statistics_board.py']),patch.object(runner,'execute',side_effect=AssertionError('不应执行')) as action,patch.object(runner.socket,'create_connection',side_effect=AssertionError('不应联网')),patch.object(runner.subprocess,'Popen',side_effect=AssertionError('不应启动工具')),contextlib.redirect_stdout(io.StringIO()) as text:
            runner.main();self.assertIn('OFFLINE_PREFLIGHT_PASS_NOT_EXECUTED',text.getvalue());action.assert_not_called()
    def test_read_plan_uses_elf_and_does_not_reset(self):
        scripts=runner.tcl_scripts(manifest,ROOT/'build/board/unit_plan_not_executed')
        for name,row in manifest['reads'].items():self.assertIn(hex(row['address'])+' '+str(row['bytes']//4),scripts['readback.tcl'])
        self.assertNotIn('rst -',scripts['readback.tcl']);self.assertNotIn('fpga -',scripts['readback.tcl']);self.assertIn('0x8002ffff',scripts['readback.tcl'])
    def test_synthetic_receipt_cannot_publish(self):
        folder=ROOT/'build/verification/statistics_board_protocol_20261024'
        folder.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(dir=folder) as name:
            p=Path(name);(p/'execution_receipt.json').write_text(json.dumps(dict(board_execution_performed=False,status='UART_AND_DDR_CAPTURED')))
            with self.assertRaises(ValueError):analysis.analyze_actual(p,manifest)

if __name__=='__main__':unittest.main(verbosity=2)
