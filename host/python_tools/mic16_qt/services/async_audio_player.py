"""非阻塞播放接口；GUI本身不构造音频设备对象。"""
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

from PySide6.QtCore import QObject, QTimer, Signal
from PySide6.QtMultimedia import QMediaPlayer


class AsyncAudioPlayer(QObject):
    positionChanged=Signal(int)
    mediaStatusChanged=Signal(object)
    errorOccurred=Signal(object,str)
    readyChanged=Signal(bool)

    def __init__(self,parent=None):
        super().__init__(parent)
        self._lock=threading.Lock();self._wake=threading.Event();self._closing=threading.Event()
        self._desired=dict(kind='state',revision=0,generation=0,path='',position_ms=0,seek_id=0,play_id=0,mode='stop',volume=.25)
        self._events={};self._process=None;self._done=threading.Event()
        self.ready=False;self.initialization_s=None;self.play_calls=0;self.log_tail=''
        self._media=QMediaPlayer.MediaStatus.NoMedia
        self._playback=QMediaPlayer.PlaybackState.StoppedState;self._has_audio=False
        self._timer=QTimer(self);self._timer.setInterval(25);self._timer.timeout.connect(self._poll);self._timer.start()
        self._thread=threading.Thread(target=self._run,daemon=True);self._thread.start()

    def _update(self,**fields):
        with self._lock:
            self._desired.update(fields);self._desired['revision']+=1
        self._wake.set()

    def setSource(self,url):
        with self._lock:
            generation=self._desired['generation']+1
        self._media=QMediaPlayer.MediaStatus.NoMedia;self._has_audio=False
        self._playback=QMediaPlayer.PlaybackState.StoppedState
        self._update(path=url.toLocalFile() if not url.isEmpty() else '',generation=generation,mode='stop')

    def setPosition(self,ms):
        with self._lock:
            serial=self._desired['seek_id']+1
        self._update(position_ms=max(0,int(ms)),seek_id=serial)

    def setVolume(self,value):self._update(volume=max(0.,min(1.,float(value))))
    def play(self):
        with self._lock:serial=self._desired['play_id']+1
        self._update(mode='play',play_id=serial)
    def pause(self):self._update(mode='pause')
    def stop(self):
        self._playback=QMediaPlayer.PlaybackState.StoppedState
        self._update(mode='stop')
    def playbackState(self):return self._playback
    def mediaStatus(self):return self._media
    def hasAudio(self):return self._has_audio

    @property
    def process_finished(self):return self._done.is_set()

    def shutdown(self):
        self._closing.set();self._wake.set();self._timer.stop()

    def _send(self,process):
        seen=-1
        try:
            while process.poll() is None:
                self._wake.wait(.05);self._wake.clear()
                if self._closing.is_set():
                    process.stdin.write(b'{"kind":"shutdown"}\n');process.stdin.flush()
                    return
                with self._lock:
                    value=dict(self._desired)
                if value['revision']!=seen:
                    process.stdin.write((json.dumps(value,ensure_ascii=False)+'\n').encode('utf-8'));process.stdin.flush()
                    seen=value['revision']
        except (OSError,ValueError):
            return

    def _run(self):
        process=None
        try:
            child=Path(__file__).with_name('audio_player_process.py')
            process=subprocess.Popen([sys.executable,'-B','-u','-X','utf8',str(child)],stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env={**os.environ,'PYTHONIOENCODING':'utf-8'},
                creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
            self._process=process
            sender=threading.Thread(target=self._send,args=(process,),daemon=True);sender.start()
            # GUI关闭后，辅助进程不能无限等待设备启动。
            def reap():
                self._closing.wait()
                if not self._done.wait(15) and process.poll() is None:process.kill()
            threading.Thread(target=reap,daemon=True).start()
            for raw in iter(lambda:process.stdout.readline(65537),b''):
                line=raw.decode('utf-8',errors='replace')
                with self._lock:
                    self.log_tail=(self.log_tail+line)[-32768:]
                if len(raw)>65536:continue
                try:
                    event=json.loads(line)
                    if event.get('kind') in ('ready','position','media','playback','error'):
                        with self._lock:self._events[event['kind']]=event
                except (ValueError,AttributeError,TypeError):pass
            code=process.wait();sender.join(1)
            if code and not self._closing.is_set():
                with self._lock:self._events['error']=dict(kind='error',value=1,message='试听子进程退出：'+str(code))
        except Exception as error:
            with self._lock:self._events['error']=dict(kind='error',value=1,message=str(error))
        finally:
            if process is not None:
                if process.poll() is None:process.kill();process.wait()
                process.stdin.close();process.stdout.close()
            self._done.set()

    def _poll(self):
        with self._lock:
            events=self._events;self._events={};generation=self._desired['generation']
        for event in events.values():
            kind=event['kind']
            if kind!='ready' and event.get('generation',generation)!=generation:continue
            if kind=='ready':
                self.ready=True;self.initialization_s=event['initialization_s'];self.readyChanged.emit(True)
            elif kind=='position':self.positionChanged.emit(event['ms'])
            elif kind=='media':
                self._media=QMediaPlayer.MediaStatus(event['value']);self._has_audio=event['has_audio']
                self.play_calls=event['play_calls'];self.mediaStatusChanged.emit(self._media)
            elif kind=='playback':self._playback=QMediaPlayer.PlaybackState(event['value'])
            elif kind=='error':self.errorOccurred.emit(QMediaPlayer.Error(event['value']),event['message'])
