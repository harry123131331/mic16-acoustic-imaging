"""精简录制标签表，保留原Tk默认值及可选二维标签。"""
from datetime import datetime

from PySide6.QtWidgets import QComboBox, QFormLayout, QLabel, QLineEdit, QPushButton, QVBoxLayout, QWidget

from mic16_capture_pipeline import CaptureLabels, DATASET_ROLES, SIGNAL_TYPES, AZEL_GEOMETRY_CONTRACT_VERSION
from mic16_qt.services.recording_labels import RecordingLabels


class RecordingLabelsPanel(QWidget):
    def __init__(self):
        super().__init__()
        layout = QVBoxLayout(self)
        note = QLabel("录制开始时固定标签；录制中不可修改。标签不改变算法，不自动认证数据或加入训练集。")
        note.setWordWrap(True)
        layout.addWidget(note)
        self.fields = QWidget()
        form = QFormLayout(self.fields)
        self.role = QComboBox()
        for key, name in zip(DATASET_ROLES, ("校准", "真实训练候选", "验证", "最终测试", "无真值诊断", "静音噪声参考")):
            self.role.addItem(f"{name} / {key}", key)
        self.signal = QComboBox()
        for key, name in zip(SIGNAL_TYPES, ("单频", "扫频", "最大长度序列", "语音", "白噪声", "粉红噪声", "静音", "其他")):
            self.signal.addItem(f"{name} / {key}", key)
        self.source_type = QComboBox()
        for key, name in (("loudspeaker", "扬声器"), ("human", "真人"), ("noise_source", "噪声源"), ("unknown", "未知")):
            self.source_type.addItem(name, key)
        self.session = QLineEdit(datetime.now().strftime("session_%Y%m%d"))
        self.scene = QLineEdit("office")
        self.source_id = QLineEdit("speaker_01")
        self.azimuth = QLineEdit("0")
        self.distance = QLineEdit("1.0")
        self.tone = QLineEdit("2000")
        self.notes = QLineEdit()
        self.elevation = QLineEdit()
        self.pose = QLineEdit()
        self.measurement = QLineEdit()
        self.geometry = QComboBox()
        self.geometry.addItem("未声明二维坐标合同", "")
        self.geometry.addItem("竖直平面阵列前半球（右/上为正）", AZEL_GEOMETRY_CONTRACT_VERSION)
        for label, widget in (("数据用途", self.role), ("信号类型", self.signal), ("声源类型", self.source_type),
                ("录制会话", self.session), ("场景", self.scene), ("声源编号（标签，不做身份识别）", self.source_id),
                ("方位角 / °（沿用旧坐标，二维需声明合同）", self.azimuth), ("距离 / m", self.distance),
                ("单频频率 / Hz", self.tone), ("备注", self.notes), ("俯仰角 / °（可选，不默认补零）", self.elevation),
                ("二维阵列摆位编号（可选）", self.pose), ("角度测量方法（可选）", self.measurement), ("二维坐标合同", self.geometry)):
            form.addRow(label, widget)
        for field, limit in ((self.session, 128), (self.scene, 128), (self.source_id, 128), (self.pose, 128),
                             (self.measurement, 256), (self.notes, 2048)):
            field.setMaxLength(limit)
        self.noise_button = QPushButton("填入静音噪声参考标签")
        self.noise_button.clicked.connect(self.set_noise_reference)
        form.addRow(self.noise_button)
        layout.addWidget(self.fields)
        layout.addStretch(1)

    def set_noise_reference(self):
        self.role.setCurrentIndex(self.role.findData("noise_reference"))
        self.signal.setCurrentIndex(self.signal.findData("silence"))
        self.source_type.setCurrentIndex(self.source_type.findData("noise_source"))
        self.source_id.setText("environment")
        for field in (self.azimuth, self.elevation, self.distance, self.pose, self.measurement):
            field.clear()
        self.geometry.setCurrentIndex(0)
        self.tone.setText("0")

    def snapshot(self) -> RecordingLabels:
        def number(field):
            value = field.text().strip()
            return float(value) if value else None
        labels = RecordingLabels(CaptureLabels(
            dataset_role=self.role.currentData(), signal_type=self.signal.currentData(),
            session_id=self.session.text().strip(), scene_id=self.scene.text().strip(), source_id=self.source_id.text().strip(),
            azimuth_deg=number(self.azimuth), distance_m=number(self.distance), tone_hz=number(self.tone) or 0.0,
            notes=self.notes.text().strip(), elevation_deg=number(self.elevation), array_pose_id=self.pose.text().strip(),
            angle_measurement_method=self.measurement.text().strip(), azel_geometry_contract=self.geometry.currentData()),
            source_type=self.source_type.currentData())
        labels.validate()
        return labels

    def set_locked(self, locked: bool):
        self.fields.setEnabled(not locked)
