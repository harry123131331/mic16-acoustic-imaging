"""由归档的真实编译命令重建PS，默认仅核对输入；不连接或下载板卡。"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,sys,time
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    group=ap.add_mutually_exclusive_group(required=True);group.add_argument('--check',action='store_true');group.add_argument('--build',action='store_true')
    ap.add_argument('--gcc',help='Vitis 2020.2的arm-none-eabi-gcc完整路径，或设置MIC16_ARM_GCC')
    args=ap.parse_args();recipe=json.loads((ROOT/'build/ps_recipe.json').read_text(encoding='utf-8'))
    for rel,digest in recipe['input_sha256'].items():
        p=(ROOT/rel).resolve()
        if not p.is_relative_to(ROOT) or not p.is_file() or sha(p)!=digest:raise ValueError('构建输入不匹配：'+rel)
    if args.check:
        print(json.dumps(dict(passed=True,input_files=len(recipe['input_sha256']),compilation_units=len(recipe['compiles']),board_accessed=False)))
        return
    compiler=args.gcc or os.environ.get('MIC16_ARM_GCC') or shutil.which('arm-none-eabi-gcc.exe')
    if not compiler or not Path(compiler).is_file():raise FileNotFoundError('请设置MIC16_ARM_GCC为Vitis 2020.2的ARM编译器')
    alias=None;actual=ROOT
    if os.name=='nt':
        alias=next((c+':' for c in 'RSTUVZ' if not Path(c+':/').exists()),None)
        if alias is None:raise RuntimeError('没有空闲短路径盘符，未覆盖任何映射')
        subprocess.run(['subst',alias,str(ROOT)],check=True,capture_output=True);actual=Path(alias+'/')
    output=ROOT/'out/ps';output.mkdir(parents=True,exist_ok=True);started=time.monotonic()
    def resolve(token):return token.replace('${root}',str(actual).replace('\\','/'))
    try:
        for i,cmd in enumerate(recipe['compiles']):
            argv=[str(compiler),*[resolve(v) for v in cmd[1:]]]
            run=subprocess.run(argv,capture_output=True,cwd=actual)
            (output/f'compile_{i:03}.log').write_bytes(run.stdout+run.stderr)
            if run.returncode:raise RuntimeError('编译失败：'+str(i)+'，见out/ps对应日志')
            if (i+1)%25==0:print('已编译 '+str(i+1)+'/'+str(len(recipe['compiles'])),flush=True)
        argv=[str(compiler),*[resolve(v) for v in recipe['link'][1:]]]
        run=subprocess.run(argv,capture_output=True,cwd=actual);(output/'link.log').write_bytes(run.stdout+run.stderr)
        if run.returncode:raise RuntimeError('链接失败，见out/ps/link.log')
        elf=output/'hemisphere_localization.elf'
        result=dict(passed=True,compilation_units=len(recipe['compiles']),ELF_sha256=sha(elf),
            build_seconds=time.monotonic()-started,source_only_rebuild=True,new_board_validation=False,board_operations=0,
            note='重建的调试路径及工具环境可影响ELF字节；未经实板验证，不自动替换配套预编译包')
        (output/'rebuild_result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print(json.dumps(result,ensure_ascii=False))
    finally:
        if alias:subprocess.run(['subst',alias,'/D'],check=True,capture_output=True)
if __name__=='__main__':main()
