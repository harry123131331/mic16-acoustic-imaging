"""精简文件处理入口；板端整数PCM仍需单独验证输入合同。"""
from pathlib import Path

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import (
    QComboBox, QDialog, QFileDialog, QFormLayout, QHBoxLayout, QLabel, QLineEdit,
    QMessageBox, QPushButton, QVBoxLayout,
)
from mic16_qt.services.per_source_job_service import PerSourceJobService, ROOT


class SourceProcessingDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle('MIC16 逐源录音处理')
        self.resize(760, 470)
        self._closing = False
        self._audio_path = None
        self.service = PerSourceJobService(self)
        layout = QVBoxLayout(self)
        notice = QLabel('后台完成多声源检测及每源重建、增强和保存，界面刷新不参与音频处理。\n'
            '提供16通道录音和独立背景；原始int32另需经核验的格式说明，不自动猜测幅值比例。')
        notice.setWordWrap(True)
        layout.addWidget(notice)
        form = QFormLayout()
        self.pcm_edit, self.quiet_edit, self.output_edit = QLineEdit(), QLineEdit(), QLineEdit(str(ROOT/'results/pc_per_source_sessions'))
        self.pose_edit = QLineEdit('fixed_array_pose0')
        self.encoding_combo=QComboBox()
        self.encoding_combo.addItem('已归一化 Float32','normalized_float32_fullscale_1')
        self.encoding_combo.addItem('原始 int32（需格式说明）','signed_int32_explicit_contract')
        self.contract_edit=QLineEdit()
        self.origin_combo = QComboBox()
        self.origin_combo.addItem('录音离线回放', 'FILE_REPLAY')
        self.origin_combo.addItem('仿真数据回放', 'SIMULATED_REPLAY')
        self._input_widgets = [self.pcm_edit, self.quiet_edit, self.output_edit, self.pose_edit, self.origin_combo,self.encoding_combo,self.contract_edit]
        for title, edit, directory in [('16通道录音', self.pcm_edit, False), ('独立背景', self.quiet_edit, False), ('结果保存目录', self.output_edit, True)]:
            row = QHBoxLayout()
            row.addWidget(edit)
            button = QPushButton('选择…')
            button.clicked.connect(lambda checked=False,e=edit,d=directory:self._choose(e,d))
            self._input_widgets.append(button)
            row.addWidget(button)
            form.addRow(title, row)
        form.addRow('安装标记', self.pose_edit)
        form.addRow('数据来源', self.origin_combo)
        form.addRow('录音格式',self.encoding_combo)
        contract_row=QHBoxLayout();contract_row.addWidget(self.contract_edit)
        self.contract_button=QPushButton('选择…');contract_row.addWidget(self.contract_button)
        self._input_widgets.append(self.contract_button)
        self.contract_button.clicked.connect(self._choose_contract)
        form.addRow('int32格式说明',contract_row)
        self.encoding_combo.currentIndexChanged.connect(self._encoding_changed)
        self._encoding_changed()
        layout.addLayout(form)
        self.state = QLabel('尚未开始。不连接板卡，不自动播放。')
        self.state.setWordWrap(True)
        self.progress = QLabel('等待输入')
        self.progress.setWordWrap(True)
        layout.addWidget(self.state)
        layout.addWidget(self.progress)
        layout.addStretch()
        row = QHBoxLayout()
        self.start_button, self.stop_button, self.open_button = QPushButton('开始处理'), QPushButton('停止'), QPushButton('查看逐源结果')
        self.stop_button.setEnabled(False)
        self.open_button.setEnabled(False)
        for widget in (self.start_button,self.stop_button,self.open_button):
            row.addWidget(widget)
        layout.addLayout(row)
        self.start_button.clicked.connect(self.start_job)
        self.stop_button.clicked.connect(self.service.cancel)
        self.open_button.clicked.connect(self.open_result)
        self.service.state_changed.connect(self.state.setText)
        self.service.progress_changed.connect(self._progress)
        self.service.finished.connect(self._finished)

    @property
    def processing_busy(self):
        return self.service.busy

    def _choose(self, edit, directory):
        path = QFileDialog.getExistingDirectory(self, '选择结果保存目录', edit.text()) if directory else QFileDialog.getOpenFileName(self, '选择Float32 NPY', edit.text(), 'NumPy (*.npy)')[0]
        if path:
            edit.setText(path)

    def _choose_contract(self):
        path=QFileDialog.getOpenFileName(self,'选择经核验的PCM格式说明','','JSON (*.json)')[0]
        if path:self.contract_edit.setText(path)

    def _encoding_changed(self):
        raw=self.encoding_combo.currentData()=='signed_int32_explicit_contract'
        self.contract_edit.setEnabled(raw);self.contract_button.setEnabled(raw)

    def start_job(self):
        try:
            if any(not edit.text().strip() for edit in (self.pose_edit,self.pcm_edit,self.quiet_edit,self.output_edit)):
                raise ValueError('请先提供录音、独立背景、结果目录和安装标记')
            request=dict(pcm_path=self.pcm_edit.text(),quiet_path=self.quiet_edit.text(),
                pose_key=self.pose_edit.text().strip(),input_origin=self.origin_combo.currentData(),
                input_encoding=self.encoding_combo.currentData())
            if request['input_encoding']=='signed_int32_explicit_contract':
                if not self.contract_edit.text().strip():raise ValueError('int32需要已确认的幅值/通道格式说明')
                request['raw_contract_path']=self.contract_edit.text()
            self.service.start(request,self.output_edit.text())
        except (OSError,ValueError,KeyError) as error:
            QMessageBox.warning(self,'无法开始',str(error))
            return
        for widget in self._input_widgets:
            widget.setEnabled(False)
        self.start_button.setEnabled(False)
        self.stop_button.setEnabled(True)
        self.open_button.setEnabled(False)
        self._audio_path = None

    def _progress(self, event):
        sources = '、'.join('ID '+str(t['source_id']) for t in event['current_confirmed']) or '无'
        stats = event['stats']
        self.progress.setText(f"已计算周期 {event['cycle']}；当前确认：{sources}\n"
            f"已写出 {stats['output_samples']:,} 点；队列峰值 {stats['peak_queued_items']} 块。显示可跳过中间进度，文件按序保存。")

    def _finished(self, result):
        for widget in self._input_widgets:
            widget.setEnabled(True)
        self.start_button.setEnabled(True)
        self.stop_button.setEnabled(False)
        self._encoding_changed()
        if result['status'] == 'COMPLETED':
            self._audio_path = Path(result['audio_path'])
            self.open_button.setEnabled(True)
            self.state.setText('处理和保存完成；可查看全部逐源文件。未进行实板或实时验收。')
            stats = result['statistics']
            self.progress.setText(f"{stats['completed_windows']} 个周期；{stats['output_samples']:,} 个输出采样点。\n结果：{self._audio_path}")
        else:
            self.state.setText(('已停止，保留不完整结果。' if result['status']=='CANCELED' else '处理失败，保留已生成文件。')+'\n'+result.get('error',''))
        if self._closing:
            QTimer.singleShot(0,self.close)

    def open_result(self):
        if self._audio_path is not None:
            self.parent().open_source_audio(self._audio_path)

    def closeEvent(self,event):
        if self.service.busy:
            self._closing = True
            self.service.cancel()
            event.ignore()
            return
        super().closeEvent(event)
