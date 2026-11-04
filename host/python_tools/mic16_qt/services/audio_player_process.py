"""独立音频播放辅助进程，全部Qt多媒体对象属于本进程及其事件循环。"""
from pathlib import Path
import json
import sys
import threading
import time

from PySide6.QtCore import QCoreApplication, QObject, QTimer, QUrl
from PySide6.QtMultimedia import QAudioOutput, QMediaPlayer

lock = threading.Lock()
latest = None
shutdown = threading.Event()


def emit(kind, **fields):
    print(json.dumps(dict(kind=kind, **fields), ensure_ascii=False),flush=True)


def read_commands():
    global latest
    try:
        while not shutdown.is_set():
            raw = sys.stdin.buffer.readline(65537)
            if not raw:
                break
            if len(raw)>65536:
                continue
            try:
                value = json.loads(raw)
                if value.get('kind')=='shutdown':
                    break
                if value.get('kind')=='state':
                    with lock:
                        latest = value
            except (ValueError,TypeError,AttributeError):
                continue
    finally:
        shutdown.set()


class Player(QObject):
    def __init__(self):
        super().__init__()
        started=time.perf_counter()
        self.output=QAudioOutput(self)
        self.output.setVolume(.25)
        self.media=QMediaPlayer(self)
        self.media.setAudioOutput(self.output)
        self.state=None;self.seek_id=-1;self.revision=-1;self.generation=0
        self.loaded=False;self.play_calls=0;self.handled_play_id=-1
        self.media.positionChanged.connect(lambda ms:emit('position',generation=self.generation,ms=ms))
        self.media.mediaStatusChanged.connect(self._status)
        self.media.playbackStateChanged.connect(lambda state:emit('playback',generation=self.generation,value=state.value))
        self.media.errorOccurred.connect(lambda error,message:emit('error',generation=self.generation,value=error.value,message=message))
        self.timer=QTimer(self);self.timer.setInterval(20);self.timer.timeout.connect(self.poll);self.timer.start()
        emit('ready',initialization_s=time.perf_counter()-started,play_calls=0)

    def _status(self,status):
        self.loaded=status in (QMediaPlayer.MediaStatus.LoadedMedia,QMediaPlayer.MediaStatus.BufferedMedia,
                               QMediaPlayer.MediaStatus.EndOfMedia)
        emit('media',generation=self.generation,value=status.value,has_audio=self.media.hasAudio(),play_calls=self.play_calls)
        if self.loaded and self.state is not None and status!=QMediaPlayer.MediaStatus.EndOfMedia:
            self._apply_transport()

    def _apply_transport(self):
        if self.state['seek_id']!=self.seek_id:
            self.media.setPosition(self.state['position_ms']);self.seek_id=self.state['seek_id']
        mode=self.state['mode']
        if mode=='play' and self.state['play_id']!=self.handled_play_id:
            self.handled_play_id=self.state['play_id'];self.play_calls+=1;self.media.play()
        elif mode=='pause':
            self.media.pause()
        elif mode=='stop' and self.media.playbackState()!=QMediaPlayer.PlaybackState.StoppedState:
            self.media.stop()

    def poll(self):
        if shutdown.is_set():
            self.media.stop();self.media.setSource(QUrl())
            emit('closed',play_calls=self.play_calls)
            QCoreApplication.quit();return
        with lock:
            value=latest
        if value is None or value.get('revision',-1)<=self.revision:
            return
        try:
            path=value['path']
            if (value['mode'] not in ('play','pause','stop') or not 0<=value['volume']<=1 or
                    type(value['position_ms']) is not int or value['position_ms']<0):
                raise ValueError('Invalid playback state')
            if path and (not Path(path).is_absolute() or not Path(path).is_file() or Path(path).suffix.lower()!='.wav'):
                raise ValueError('Only existing local WAV files can be selected')
            changed=value['generation']!=self.generation
            self.state=value;self.revision=value['revision'];self.output.setVolume(value['volume'])
            if changed:
                self.media.stop();self.generation=value['generation'];self.seek_id=-1;self.loaded=False
                self.media.setSource(QUrl.fromLocalFile(path) if path else QUrl())
            elif self.loaded:
                self._apply_transport()
        except (KeyError,TypeError,ValueError) as error:
            emit('error',generation=self.generation,value=1,message=str(error))


app=QCoreApplication([])
threading.Thread(target=read_commands,daemon=True).start()
player=Player()
if not shutdown.is_set():
    raise SystemExit(app.exec())
