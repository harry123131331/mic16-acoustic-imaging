"""Saved per-source result loading and silent Qt selection, never sound playback."""
from __future__ import annotations

import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
import numpy as np
from PySide6.QtWidgets import QApplication
from PySide6.QtMultimedia import QMediaPlayer
from mic16_qt.services.source_audio_session import SourceAudioSession
from mic16_qt.presentation.compact_window import CompactWindow


def write_json(path, data):
    path.write_text(json.dumps(data), encoding='utf-8')


def fixture(root):
    n = 32000
    rows = []
    for sid, kind in [(None, 'input_MIC0'), (1, 'WDAS'), (1, 'enhanced'), (2, 'WDAS'), (2, 'enhanced')]:
        name = f'{sid}_{kind}.wav'
        audio = np.full(n, .01 * (sid or 5), dtype='<f4')
        if sid == 2:
            audio[:16000] = 0
        if kind == 'enhanced':
            audio *= .5
        data = audio.tobytes()
        (root / name).write_bytes(b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVEfmt ' +
            struct.pack('<IHHIIHH', 16, 3, 1, 16000, 64000, 4, 32) + b'data' + struct.pack('<I', len(data)) + data)
        rows.append(dict(path=name, source_id=sid, kind=kind, frames=n, sample_rate=16000))
    (root / 'audio_files.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in rows), encoding='utf-8')
    packets = [dict(segment_id='test', pose_key='P0', output_rate=16000,
        output_start_sample=i*16000, output_end_sample_exclusive=(i+1)*16000,
        current_confirmed=[dict(source_id=1)] + ([dict(source_id=2)] if i else [])) for i in range(2)]
    (root / 'packets.jsonl').write_text(''.join(json.dumps(row) + '\n' for row in packets), encoding='utf-8')
    write_json(root / 'session.json', dict(schema_version=1, status='COMPLETED', segment_id='test',
        pose_key='P0', evidence_source='SIMULATED_REPLAY', audio_rate=16000, output_samples=n,
        audio_files=5, packets=2, activity_metadata='packets.jsonl', audio_manifest='audio_files.jsonl'))


class SourceAudioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.app = QApplication.instance() or QApplication([])

    def setUp(self):
        parent = TOOLS.parent / 'results/test_tmp'
        parent.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='source_audio_', dir=parent)
        self.root = Path(self.temp.name)
        fixture(self.root)

    def tearDown(self):
        self.temp.cleanup()

    def test_files_are_independent_bounded_and_keep_inactive_zeros(self):
        session = SourceAudioSession.load(self.root)
        self.assertEqual(session.source_ids, [1, 2])
        self.assertEqual(session.first_confirmation, {1: 0, 2: 16000})
        one = session.assets[1, 'enhanced']
        two = session.assets[2, 'enhanced']
        self.assertEqual(len(one.preview(0, 1000000)[1]), 32000)
        np.testing.assert_array_equal(two.preview(0)[1], np.zeros(16000, dtype=np.float32))
        np.testing.assert_allclose(one.preview()[1], .005)
        np.testing.assert_allclose(two.preview(16000)[1], .01)

    def test_rejects_path_escape_and_truncated_WAV(self):
        path = self.root / 'audio_files.jsonl'
        original = path.read_text()
        path.write_text(original.replace('1_WDAS.wav', '../outside.wav'), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, '目录'):
            SourceAudioSession.load(self.root)
        path.write_text(original, encoding='utf-8')
        wav = self.root / '1_WDAS.wav'
        wav.write_bytes(wav.read_bytes()[:-4])
        with self.assertRaisesRegex(ValueError, 'RIFF'):
            SourceAudioSession.load(self.root)

    def test_rejects_packet_gap_and_incomplete_session(self):
        path = self.root / 'packets.jsonl'
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        rows[1]['output_start_sample'] += 1
        path.write_text(''.join(json.dumps(row) + '\n' for row in rows), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, '缺口'):
            SourceAudioSession.load(self.root)
        header = self.root / 'session.json'
        info = json.loads(header.read_text())
        info['status'] = 'INCOMPLETE'
        write_json(header, info)
        with self.assertRaisesRegex(ValueError, '完整保存'):
            SourceAudioSession.load(self.root)

    def test_compact_opener_switches_source_and_kind_without_play_or_inference(self):
        before = {p.name: p.read_bytes() for p in self.root.iterdir()}
        window = CompactWindow()
        window._timer.stop()
        dialog = window.open_source_audio(self.root)
        try:
            self.assertIsNotNone(dialog)
            self.assertEqual(dialog.player.playbackState(), QMediaPlayer.PlaybackState.StoppedState)
            first = dialog.asset.path
            dialog.source_combo.setCurrentIndex(1)
            self.assertNotEqual(first, dialog.asset.path)
            self.assertEqual(dialog.asset.source_id, 2)
            x, y = dialog.curve.getData()
            self.assertEqual(x[0], 1.)
            np.testing.assert_allclose(y, .01)
            dialog.kind_combo.setCurrentIndex(1)
            self.assertEqual(dialog.asset.kind, 'WDAS')
            dialog.kind_combo.setCurrentIndex(2)
            self.assertEqual(dialog.asset.kind, 'input_MIC0')
            self.assertFalse(dialog.source_combo.isEnabled())
            self.assertEqual(dialog.player.playbackState(), QMediaPlayer.PlaybackState.StoppedState)
            self.assertIsNone(window._udp_worker)
            self.assertEqual(window.source_count.text(), '—')
            self.assertEqual(before, {p.name: p.read_bytes() for p in self.root.iterdir()})
        finally:
            dialog.close()
            window.close()
            QApplication.processEvents()


if __name__ == '__main__':
    unittest.main(verbosity=2)
