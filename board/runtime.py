"""参赛副本的文件核验与人工JTAG加载；--check不会访问网络或板卡。"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import socket
import subprocess
import time
import sys

ROOT=Path(__file__).resolve().parents[1]


def check():
    manifest=json.loads((ROOT/'board/prebuilt/runtime_manifest.json').read_text(encoding='utf-8'))
    for name,expected in manifest.items():
        p=ROOT/'board/prebuilt'/name
        if hashlib.sha256(p.read_bytes()).hexdigest()!=expected:raise ValueError('运行文件不匹配：'+name)
    print(json.dumps(dict(passed=True,offline_only=True,board_accessed=False,files=len(manifest)),ensure_ascii=False))


def listening():
    try:
        with socket.create_connection(('127.0.0.1',3121),.2):return True
    except OSError:return False


def load():
    # 只有人工选择--load才会准备服务、建立短路径和执行XSCT。
    xsct=os.environ.get('MIC16_XSCT') or shutil.which('xsct.bat')
    if not xsct:
        install=os.environ.get('XILINX_VITIS')
        if install:xsct=str(Path(install)/'bin/xsct.bat')
    if not xsct or not Path(xsct).is_file():
        raise FileNotFoundError('请设置MIC16_XSCT为Vitis 2020.2的bin/xsct.bat完整路径')
    if not listening():
        server=Path(os.environ.get('MIC16_HW_SERVER',str(Path(xsct).with_name('hw_server.bat'))))
        if not server.is_file():raise FileNotFoundError('没有找到原厂hw_server.bat，请设置MIC16_HW_SERVER')
        logs=ROOT/'logs';logs.mkdir(exist_ok=True)
        with (logs/'hw_server.log').open('ab') as stream:
            subprocess.Popen([os.environ.get('ComSpec','cmd.exe'),'/d','/c',str(server),'-s','tcp::3121'],
                             stdout=stream,stderr=stream,creationflags=subprocess.CREATE_NO_WINDOW)
        deadline=time.monotonic()+5
        while not listening():
            if time.monotonic()>=deadline:raise TimeoutError('原厂调试服务未就绪，请查看logs/hw_server.log')
            time.sleep(.1)
    alias=next((c+':' for c in ('R','S','T','U','V','Z') if not Path(c+':/').exists()),None)
    if alias is None:raise RuntimeError('没有空闲短路径盘符，未更改已有映射')
    subprocess.run(['subst',alias,str(ROOT)],check=True,capture_output=True)
    try:
        result=subprocess.run([xsct,alias+'/board/load_runtime.tcl'],capture_output=True,timeout=120)
        logs=ROOT/'logs';logs.mkdir(exist_ok=True)
        (logs/'firmware_load.log').write_bytes(result.stdout+result.stderr)
        if result.returncode:raise RuntimeError('加载未完成，请查看logs/firmware_load.log')
        print('匹配程序已加载到待机。请打开Qt，连接后手动开始；没有写Flash或自动采集。')
    finally:
        subprocess.run(['subst',alias,'/D'],check=True,capture_output=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    action=parser.add_mutually_exclusive_group(required=True)
    action.add_argument('--check',action='store_true')
    action.add_argument('--load',action='store_true')
    args=parser.parse_args();check()
    if args.load:
        load()
        subprocess.Popen([sys.executable,'-B','-X','utf8',str(ROOT/'host/python_tools/mic16_qt_gui.py'),'--localization-demo'],cwd=ROOT,creationflags=subprocess.CREATE_NO_WINDOW)
