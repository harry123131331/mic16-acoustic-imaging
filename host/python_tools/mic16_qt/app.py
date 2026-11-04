from __future__ import annotations

import argparse
import json
import time
import os
import sys
from pathlib import Path

import pyqtgraph as pg
from PySide6.QtCore import QTimer, Qt, QObject, QEvent, QLockFile, QStandardPaths
from PySide6.QtNetwork import QLocalServer, QLocalSocket
from PySide6.QtGui import QFont, QFontDatabase
from PySide6.QtWidgets import QApplication

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="MIC16 定位优先的精简上位机")
    parser.add_argument("--localization-demo", action="store_true", help="板卡持续定位、原始波形和可选USB摄像头热图叠加")
    parser.add_argument("--research-ui", action="store_true", help="打开旧七页研究界面，日常默认不加载")
    parser.add_argument(
        "--screenshot",
        type=Path,
        help="保存确定性的界面截图后退出",
    )
    parser.add_argument(
        "--quit-after-ms",
        type=int,
        default=0,
        help="经过指定毫秒数后自动退出",
    )
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--replay", type=Path, help="打开 raw_i32.npy 离线回放")
    source.add_argument("--udp", action="store_true", help="启动板端 UDP 监听")
    source.add_argument("--demo", action="store_true", help="显式启动演示数据（非实测）")
    source.add_argument("--heatmap-replay",type=Path,help="打开已有C空间响应回放，不启动模型或板卡")
    parser.add_argument("--listen-ip", default="0.0.0.0", help="UDP 监听地址")
    parser.add_argument("--port", type=int, default=5000, help="UDP 监听端口")
    parser.add_argument("--startup-metrics",type=Path,help="记录软件首屏耗时，不加载板卡")
    return parser


def fit_main_window(window, screen):
    """所有尺寸使用Qt逻辑坐标，给标题栏和任务栏留出空间。"""
    if screen is None:
        return
    area = screen.availableGeometry()
    width = max(320, area.width() - 48)
    height = max(240, area.height() - 80)
    window.setMinimumSize(min(1060, width), min(680, height))
    window.resize(min(1380, width), min(860, height))
    window.move(area.x() + max(12, (area.width()-window.width())//2),
                area.y() + max(28, (area.height()-window.height())//2))


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.localization_demo and (args.research_ui or args.udp or args.replay or args.demo or args.heatmap_replay):
        parser.error("定位演示从待机打开，请在界面连接并开始；不能同时启用回放或演示数据")
    QApplication.setHighDpiScaleFactorRoundingPolicy(
        Qt.HighDpiScaleFactorRoundingPolicy.PassThrough
    )
    application_type = QApplication
    if args.localization_demo:
        from mic16_qt.presentation.localization_demo import LocalizationDemoApplication
        application_type = LocalizationDemoApplication
    app = application_type(sys.argv[:1])
    # 日常入口保持单实例；重复双击只激活常驻窗口，不占用第二组UDP端口。
    server=None
    instance_lock=None
    if args.screenshot is None and args.quit_after_ms<=0:
        instance_lock=QLockFile(str(Path(QStandardPaths.writableLocation(QStandardPaths.StandardLocation.TempLocation))/'mic16_acoustic_console.lock'))
        instance_lock.setStaleLockTime(0)
        owns_instance=instance_lock.tryLock(0)
        probe=QLocalSocket();probe.connectToServer('MIC16-acoustic-imaging-console')
        if probe.waitForConnected(100):
            probe.write(b'ACTIVATE');probe.waitForBytesWritten(100);probe.disconnectFromServer();return 0
        if not owns_instance:
            # 另一实例刚开始加载时也不创建第二个窗口或抢占其本地服务。
            return 0
        QLocalServer.removeServer('MIC16-acoustic-imaging-console')
        server=QLocalServer()
        if not server.listen('MIC16-acoustic-imaging-console'):
            raise RuntimeError('无法建立常驻软件入口：'+server.errorString())
    app.setApplicationName("MIC16 声源定位")
    app.setOrganizationName("MIC16 Research")
    font_family = _install_cjk_font()
    app.setFont(QFont(font_family, 10))
    pg.setConfigOptions(antialias=True, imageAxisOrder="row-major")

    source_mode = "udp" if args.udp else ("replay" if args.replay else ("synthetic" if args.demo else "idle"))
    if args.research_ui:
        from mic16_qt.presentation.main_window import MainWindow
        from mic16_qt.presentation.theme import application_stylesheet

        app.setStyleSheet(application_stylesheet(font_family))
        window = MainWindow(autostart=True, source_mode=source_mode if source_mode!="idle" else "synthetic",
                            replay_path=args.replay,listen_ip=args.listen_ip,port=args.port)
    else:
        if args.localization_demo:
            from mic16_qt.presentation.localization_demo import LocalizationDemoWindow as CompactWindow
        else:
            from mic16_qt.presentation.compact_window import CompactWindow
        from mic16_qt.presentation.compact_theme import compact_stylesheet

        app.setStyleSheet(compact_stylesheet(font_family))
        window = CompactWindow(source_mode=source_mode,replay_path=args.replay,
                               listen_ip=args.listen_ip,port=args.port)
    if args.localization_demo:
        app.observed_window = window
    if server is not None:
        def activate_existing():
            while server.hasPendingConnections():
                client=server.nextPendingConnection();client.disconnectFromServer();client.deleteLater()
            if window.isMinimized():window.showNormal()
            window.raise_();window.activateWindow()
        server.newConnection.connect(activate_existing)
    class FirstPaint(QObject):
        def eventFilter(self,watched,event):
            if event.type()==QEvent.Type.Paint:
                watched.removeEventFilter(self)
                if args.startup_metrics:
                    begin=int(os.environ.get('MIC16_GUI_ENTRY_NS',time.perf_counter_ns()))
                    record=dict(status='FIRST_WINDOW_PAINT',software_startup_ms=(time.perf_counter_ns()-begin)/1e6,
                        board_programmed=False,audio_capture_started=False,network_connected=False,
                        definition='从Python入口至主窗口首次绘制；不包含首次下载/连接设备')
                    path=args.startup_metrics.resolve();path.parent.mkdir(parents=True,exist_ok=True)
                    if path.exists():raise FileExistsError(path)
                    path.write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
            return False
    paint_observer=FirstPaint(window);window.installEventFilter(paint_observer)
    if not args.research_ui and args.screenshot is None and args.quit_after_ms <= 0:
        fit_main_window(window, app.primaryScreen())
        window.showMaximized()
    else:
        window.show()
    if args.heatmap_replay and not args.research_ui:window.load_heatmap_replay(args.heatmap_replay)

    if args.screenshot is not None:
        destination = args.screenshot.resolve()

        def capture_and_exit() -> None:
            if not window.save_screenshot(destination):
                print(f"ERROR: could not save screenshot: {destination}", file=sys.stderr)
                app.exit(2)
                return
            print(f"SCREENSHOT={destination}")
            window.close()

        QTimer.singleShot(1300, capture_and_exit)
    elif args.quit_after_ms > 0:
        QTimer.singleShot(args.quit_after_ms, window.close)

    return app.exec()


def _install_cjk_font() -> str:
    """Qt字体库为空时显式加载Windows中文字体。离屏截图环境可能找不到系统字体，显式加载可保证自动截图可读，同时不在工程中分发专有字体。"""

    candidates = (
        Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts" / "msyh.ttc",
        Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts" / "Deng.ttf",
        Path(os.environ.get("WINDIR", r"C:\Windows")) / "Fonts" / "simhei.ttf",
    )
    for candidate in candidates:
        if not candidate.is_file():
            continue
        font_id = QFontDatabase.addApplicationFont(str(candidate))
        if font_id < 0:
            continue
        families = QFontDatabase.applicationFontFamilies(font_id)
        if families:
            return families[0]
    return "Segoe UI"


if __name__ == "__main__":
    raise SystemExit(main())
