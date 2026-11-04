"""Qt事件驱动的板端控制；不下载固件、不采背景、不靠发包成功冒充启动成功。"""
import ipaddress
import secrets
import time
from PySide6.QtCore import QObject, QTimer, Signal
from PySide6.QtNetwork import QAbstractSocket, QHostAddress, QUdpSocket
from mic16_qt.services.control_wire import Capability, DeviceState, Operation, PORT, command, decode_status


class DeviceControl(QObject):
    changed = Signal()
    error = Signal(str)

    def __init__(self, parent=None, *, timeout_s=1., retries=1):
        super().__init__(parent)
        self.timeout_s, self.retries = float(timeout_s), int(retries)
        self.socket = QUdpSocket(self)
        self.socket.readyRead.connect(self._receive)
        self.timer = QTimer(self)
        self.timer.setInterval(100)
        self.timer.timeout.connect(self.service)
        self.connected = False
        self.status = None
        self.pending = None
        self.message = '板端控制未连接'
        self.last_received = None
        self.rejected = 0
        self._request = 0
        self._last_query = 0.

    def open(self, listen_ip, board_ip, port=PORT):
        self.close()
        listen_ip, board_ip = str(ipaddress.IPv4Address(listen_ip)), str(ipaddress.IPv4Address(board_ip))
        if not 1 <= int(port) <= 65535:
            raise ValueError('板端控制端口无效')
        if not self.socket.bind(QHostAddress(listen_ip), 0, QAbstractSocket.BindFlag.DontShareAddress):
            raise OSError(self.socket.errorString())
        self.board_ip, self.port = board_ip, int(port)
        self.nonce = secrets.randbits(64) or 1
        self.connected = True
        self._request = 0
        self._last_query = 0.
        self.message = '正在查询板端程序…'
        self._send(Operation.HELLO)
        self.timer.start()

    def close(self):
        self.timer.stop()
        self.socket.close()
        self.connected = False
        self.status = None
        self.pending = None
        self.last_received = None
        self.message = '板端控制未连接'

    def _send(self, operation):
        if not self.connected:
            return False
        if self.pending is not None:
            # 用户启停可替代只读查询；旧查询回复会因请求号不匹配而被忽略。
            if self.pending['operation'] != Operation.STATUS or operation not in (Operation.START, Operation.STOP):
                return False
        self._request = self._request % 0xffffffff + 1
        boot = self.status.boot if self.status else 0
        raw = command(operation, self._request, boot, self.nonce)
        self.pending = dict(operation=operation, request=self._request, data=raw, sent=time.monotonic(), retries=0)
        if self.socket.writeDatagram(raw, QHostAddress(self.board_ip), self.port) != len(raw):
            self.pending = None
            self.message = '控制命令发送失败：' + self.socket.errorString()
            self.error.emit(self.message)
            return False
        self._last_query = time.monotonic()
        self.changed.emit()
        return True

    @property
    def responsive(self):
        return self.status is not None and self.last_received is not None and time.monotonic() - self.last_received < 3.5

    @property
    def _control_available(self):
        return self.pending is None or self.pending['operation'] == Operation.STATUS

    @property
    def can_start(self):
        required = Capability.CONTINUOUS | Capability.SOURCE_AUDIO
        return bool(self.responsive and self._control_available and self.status.code == 0
                    and (self.status.capabilities & required == required or self.status.capabilities & (Capability.LOCALIZATION | Capability.RAW_PCM) == (Capability.LOCALIZATION | Capability.RAW_PCM))
                    and self.status.state == DeviceState.IDLE)

    @property
    def can_stop(self):
        return bool(self.responsive and self._control_available and self.status.state in (DeviceState.STARTING, DeviceState.RUNNING))

    def start(self):
        if not self.can_start:
            return False
        self.message = '等待板端确认开始定位…'
        return self._send(Operation.START)

    def stop(self):
        if not self.can_stop:
            return False
        self.message = '等待板端停止并排空…'
        return self._send(Operation.STOP)

    def service(self):
        if not self.connected:
            return
        now = time.monotonic()
        if self.pending is not None:
            if now - self.pending['sent'] < self.timeout_s:
                return
            if self.pending['retries'] < self.retries:
                self.socket.writeDatagram(self.pending['data'], QHostAddress(self.board_ip), self.port)
                self.pending['sent'] = now
                self.pending['retries'] += 1
                return
            operation = self.pending['operation']
            self.pending = None
            if operation in (Operation.START, Operation.STOP):
                self.message = '板端命令未获确认；实际运行状态未知'
                self.status = None
            else:
                self.message = '板端未响应：请检查网线、IP及是否已加载常驻固件'
            self.changed.emit()
            return
        if now - self._last_query >= 1.:
            self._send(Operation.STATUS if self.status is not None else Operation.HELLO)

    def _receive(self):
        while self.socket.hasPendingDatagrams():
            packet = self.socket.receiveDatagram()
            peer = packet.senderAddress().toString().removeprefix('::ffff:')
            try:
                if peer != self.board_ip or packet.senderPort() != self.port:
                    raise ValueError('控制来源不匹配')
                status = decode_status(bytes(packet.data()))
                pending = self.pending
                if pending is None or status.nonce != self.nonce or status.request != pending['request'] or status.operation != pending['operation']:
                    raise ValueError('控制回复不是当前请求')
                if self.status is not None and status.boot != self.status.boot and status.operation != Operation.HELLO:
                    # 重启后重新握手，不把原请求确认成新固件执行结果。
                    self.status = None
                    self.pending = None
                    self.message = '板卡已重启，正在重新确认程序'
                    self._send(Operation.HELLO)
                    continue
            except ValueError:
                self.rejected += 1
                continue
            self.status = status
            self.pending = None
            self.last_received = time.monotonic()
            names = {DeviceState.IDLE:'板端已就绪', DeviceState.STARTING:'板端正在启动', DeviceState.RUNNING:'板端正在运行',
                     DeviceState.DRAINING:'板端正在排空', DeviceState.FAULT:'板端故障'}
            self.message = names[status.state]
            if status.capabilities & Capability.SINGLE_CAPTURE:self.message += ' · 单次诊断，非持续实时'
            elif status.capabilities & Capability.LOCALIZATION:self.message += ' · 板端仅定位，增强在PC侧'
            if status.code:
                reasons = {-64:'采集链中断或缓冲不足', -63:'输入缓冲已满', -65:'处理结果超时',
                           -506:'采集缓冲溢出', -508:'音频DMA超时'}
                detail = reasons.get(status.code, '请查看板端诊断记录')
                self.message += f'（状态码{status.code}：{detail}）'
            elif not status.capabilities & (Capability.CONTINUOUS | Capability.SINGLE_CAPTURE):
                self.message += ' · 当前固件不支持持续定位'
            self.changed.emit()
