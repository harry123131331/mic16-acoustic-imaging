"""Manual room parameters, geometry and bounded optional RIR generation.

Default operation validates only. No model, training or old dataset mutation.
The fixed 16-microphone geometry is kept separate from room pose.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import re
import sys
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG = ROOT / 'config/simulation/meeting_room_manual.json'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def number(value, name):
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(name + ' 必须是有限数字')
    return float(value)


def vector(value, name):
    if not isinstance(value, list) or len(value) != 3:
        raise ValueError(name + ' 必须是[x,y,z]三个数字，单位米')
    return np.array([number(x, name) for x in value])


def keys(value, required, optional, name):
    if not isinstance(value, dict) or set(value) - set(required) - set(optional) or set(required) - set(value):
        raise ValueError(name + ' 字段缺失或存在未知字段')


def unique_object(items):
    result = {}
    for key, value in items:
        if key in result:
            raise ValueError('JSON字段重复: ' + key)
        result[key] = value
    return result


def rotation_matrix(yaw, pitch, roll):
    """Active right-handed Rz(yaw) @ Ry(pitch) @ Rx(roll), local to room."""
    y, p, r = np.deg2rad([yaw, pitch, roll])
    rz = np.array([[np.cos(y), -np.sin(y), 0], [np.sin(y), np.cos(y), 0], [0, 0, 1]])
    ry = np.array([[np.cos(p), 0, np.sin(p)], [0, 1, 0], [-np.sin(p), 0, np.cos(p)]])
    rx = np.array([[1, 0, 0], [0, np.cos(r), -np.sin(r)], [0, np.sin(r), np.cos(r)]])
    return rz @ ry @ rx


def validate(config):
    keys(config, ('schema_version', 'scene_id', 'room', 'array_pose', 'sources', 'rir', 'background'),
         ('notes_zh',), '配置')
    if type(config['schema_version']) is not int or config['schema_version'] != 1:
        raise ValueError('schema_version 必须是1')
    if not isinstance(config['scene_id'], str) or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]{0,63}', config['scene_id']):
        raise ValueError('scene_id 使用字母开头的英文、数字或下划线')
    room, pose, rir, bg = (config[k] for k in ('room', 'array_pose', 'rir', 'background'))
    keys(room, ('dimensions_m', 'target_rt60_s', 'sound_speed_m_s', 'minimum_wall_clearance_m'), (), 'room')
    keys(pose, ('center_m', 'yaw_deg', 'pitch_deg', 'roll_deg'), (), 'array_pose')
    keys(rir, ('sample_rate_hz', 'duration_s', 'reflection_order', 'hp_filter'), (), 'rir')
    keys(bg, ('mode', 'digital_rms', 'ar1_coefficient', 'seed'), (), 'background')
    dims, center = vector(room['dimensions_m'], 'room.dimensions_m'), vector(pose['center_m'], 'array_pose.center_m')
    clearance = number(room['minimum_wall_clearance_m'], 'minimum_wall_clearance_m')
    c, rt60 = number(room['sound_speed_m_s'], 'sound_speed_m_s'), number(room['target_rt60_s'], 'target_rt60_s')
    if np.any(dims <= 0) or clearance < 0 or c <= 0 or rt60 < 0:
        raise ValueError('房间尺寸和声速必须>0，离墙距离及RT60必须>=0')
    if rt60 > 0:
        surface = 2 * (dims[0]*dims[1] + dims[0]*dims[2] + dims[1]*dims[2])
        alpha = 24 * np.log(10.) * float(np.prod(dims)) / (c * surface * rt60)
        if alpha > 1:
            raise ValueError('该尺寸与RT60无法由当前RIR模型生成：增大RT60或缩小房间')
    rotation = rotation_matrix(*(number(pose[k], k) for k in ('yaw_deg', 'pitch_deg', 'roll_deg')))
    phase = np.arange(16) * 2 * np.pi / 16
    local_mics = np.column_stack([.08 * np.cos(phase), np.zeros(16), .08 * np.sin(phase)])
    mics = center + local_mics @ rotation.T
    if np.any(center < 0) or np.any(center > dims) or np.min(np.minimum(mics, dims - mics)) < clearance - 1e-12:
        raise ValueError('阵列中心/旋转后的麦克风越界，或不满足minimum_wall_clearance_m')
    fs, duration = number(rir['sample_rate_hz'], 'sample_rate_hz'), number(rir['duration_s'], 'duration_s')
    if fs <= 0 or duration <= 0 or type(rir['reflection_order']) is not int or rir['reflection_order'] < -1 or type(rir['hp_filter']) is not bool:
        raise ValueError('采样率/时长必须>0，反射阶数为>=-1整数，hp_filter为true/false')
    if bg['mode'] != 'independent_ar1' or not 0 <= number(bg['digital_rms'], 'digital_rms') < 1 or not abs(number(bg['ar1_coefficient'], 'ar1_coefficient')) < 1 or type(bg['seed']) is not int or bg['seed'] < 0:
        raise ValueError('背景需独立AR(1)、0<=digital_rms<1、|ar1_coefficient|<1和非负整数seed')
    if not isinstance(config['sources'], list) or len(config['sources']) > 8:
        raise ValueError('sources需为0至8个源的列表；当前模型仅验证了0至3源')
    sources, ids, warnings = [], set(), []
    if fs != 39062.5:
        warnings.append('采样率不同于当前检测模型39062.5Hz，不能直接作为原模型输入')
    if len(config['sources']) > 3:
        warnings.append('超过当前已验证的最多3个活动源，不代表模型支持更多源')
    warnings.append('背景模式为通道独立AR(1)，室内空间相关噪声仍待实现')
    max_delay_s = 0.
    for source in config['sources']:
        keys(source, ('id', 'position_m'), (), 'source')
        sid = source['id']
        if not isinstance(sid, str) or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_-]{0,31}', sid) or sid in ids:
            raise ValueError('声源id必须唯一，并使用英文/数字/下划线')
        ids.add(sid)
        pos = vector(source['position_m'], sid + '.position_m')
        if np.min(np.minimum(pos, dims-pos)) < clearance - 1e-12:
            raise ValueError(sid + ' 越出房间或离墙过近；改房间尺寸后需同时检查摆放坐标')
        local = rotation.T @ (pos - center)
        distance = float(np.linalg.norm(local))
        mic_distances = np.linalg.norm(mics - pos, axis=1)
        if distance <= .08 or np.min(mic_distances) <= 1e-6:
            raise ValueError(sid + ' 需在阵列圆周以外，不能与麦克风重合')
        az = float(np.rad2deg(np.arctan2(local[0], local[1])))
        el = float(np.rad2deg(np.arctan2(local[2], np.hypot(local[0], local[1]))))
        in_fov = -60 <= az <= 60 and -30 <= el <= 30
        if not in_fov:
            warnings.append(sid + ' 位于当前搜索视场外，保留真实标签，不删除该源')
        max_delay_s = max(max_delay_s, float(np.max(mic_distances)) / c)
        sources.append(dict(**source, azimuth_deg=az, elevation_deg=el, distance_m=distance,
                            inside_current_search_FOV=in_fov, geometric_arrival_samples=(mic_distances * fs / c).tolist()))
    if duration < rt60 + max_delay_s:
        warnings.append('RIR时长短于目标RT60加传播时间，需检查尾声截断')
    raw_bytes = round(fs * duration) * 16 * 4 * len(sources)
    if round(fs * duration) < 2 or raw_bytes > 64 * 1024**2:
        raise ValueError('至少2采样点；单配置RIR原始float32预算最多64MiB，缩短时长或减少源')
    return dict(status='CONFIG_VALID_GEOMETRY_ONLY', scene_id=config['scene_id'], config=config,
                room_size_m=dims.tolist(), array_center_m=center.tolist(), rotation_matrix=rotation.tolist(),
                array_front_in_room=(rotation @ np.array([0., 1., 0.])).tolist(),
                microphone_positions_m=mics.tolist(), sources=sources, warnings=warnings,
                proposed_raw_RIR_float32_bytes=raw_bytes, sample_count=round(fs*duration),
                source_count_is_registered_positions_not_activity_labels=True,
                acoustic_data_generated=False, model_performance_claimed=False)


def load_config(path):
    content = Path(path).read_bytes()
    config = json.loads(content.decode('utf-8-sig'), object_pairs_hook=unique_object)
    return dict(**validate(config), config_sha256=hashlib.sha256(content).hexdigest())


def background_from_config(config, sample_count):
    """Reusable mixture-stage hook: actual digital RMS knob, no point-source DOA."""
    from scipy.signal import lfilter
    validate(config)
    if type(sample_count) is not int or not 1 <= sample_count <= 1000000:
        raise ValueError('sample_count需为1至1000000整数')
    bg = config['background']
    x = lfilter([1.], [1., -bg['ar1_coefficient']], np.random.default_rng(bg['seed']).standard_normal((sample_count, 16)), axis=0)
    return (x * (bg['digital_rms'] / np.sqrt(np.mean(x*x)))).astype(np.float32)


def rir_arguments(state, source):
    config = state['config']
    return dict(c=config['room']['sound_speed_m_s'], fs=config['rir']['sample_rate_hz'],
                r=np.asarray(state['microphone_positions_m']), s=np.asarray(source['position_m']),
                L=np.asarray(state['room_size_m']), reverberation_time=config['room']['target_rt60_s'],
                nsample=state['sample_count'], order=config['rir']['reflection_order'], dim=3,
                hp_filter=config['rir']['hp_filter'])


def new_run_directory(state, kind, require_new=True):
    if kind not in ('geometry', 'rir'):
        raise ValueError('未知输出类型')
    output = ROOT / 'results/manual_room_config_runs' / (state['scene_id'] + '_' + state['config_sha256'][:16] + '_' + kind)
    output.resolve().relative_to((ROOT / 'results/manual_room_config_runs').resolve())
    if require_new and output.exists():
        raise ValueError('相同配置的结果已存在；保留已有结果，修改scene_id或参数后再生成')
    return output


def generate_rirs(state, config_path):
    if not state['sources']:
        raise ValueError('当前配置没有声源位置，无需生成RIR')
    if digest(config_path) != state['config_sha256']:
        raise ValueError('校验后配置被修改，请重新执行')
    output = new_run_directory(state, 'rir')
    # 只使用已安装且锁定的运行环境，不下载或更改依赖。
    runtime = ROOT / '.runtime/rir_generator_0_3_0'
    lock = json.loads((ROOT / 'results/candidate_rir_preparation_20261007/protocol_lock.json').read_text(encoding='utf-8'))
    for info in lock['pinned_package'].values():
        if digest(Path(info['path'])) != info['sha256']:
            raise ValueError('原固定RIR运行库哈希不一致')
    sys.path.insert(0, str(runtime))
    import rir_generator as generator
    if generator.__version__ != '0.3.0':
        raise ValueError('需要已固定的rir_generator0.3.0')
    output.mkdir(parents=True)
    # 生成新声学数据前先锁定实际修改后的配置。
    write_json(output / 'config_lock.json', dict(config_sha256=state['config_sha256'], config=state['config'],
        geometry=state, generator='rir_generator0.3.0', status='REGISTERED_BEFORE_RIR_GENERATION',
        mixture_windows=0, training_runs=0, original_baseline_modified=False))
    assets = []
    for source in state['sources']:
        h = generator.generate(**rir_arguments(state, source), mtype=generator.mtype.omnidirectional)
        if h.shape != (state['sample_count'], 16) or not np.isfinite(h).all():
            raise ValueError('生成RIR的形状或数值异常')
        p = output / (source['id'] + '_rir.npz')
        np.savez_compressed(p, rir=h.astype(np.float32), room_size_m=state['room_size_m'],
                            microphone_positions_m=state['microphone_positions_m'], source_position_m=source['position_m'],
                            truth_azimuth_deg=source['azimuth_deg'], truth_elevation_deg=source['elevation_deg'],
                            sample_rate_hz=state['config']['rir']['sample_rate_hz'],
                            array_center_m=state['array_center_m'], source_range_m=source['distance_m'],
                            target_rt60_s=state['config']['room']['target_rt60_s'],
                            sound_speed_m_s=state['config']['room']['sound_speed_m_s'])
        assets.append(dict(id=source['id'], file=p.name, sha256=digest(p)))
    write_json(output / 'generation.json', dict(status='GENERATED_RIRS_NOT_VALIDATED_NOT_TRAINED',
        assets=assets, RIR_count=len(assets), mixture_windows=0, training_runs=0,
        original_baseline_modified=False, model_performance_claimed=False))
    return output


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description='手动房间配置：默认仅校验，音频/训练均不自动执行')
    parser.add_argument('--config', type=Path, default=DEFAULT_CONFIG)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--export-geometry', action='store_true', help='导出小型几何JSON，供MATLAB预览')
    action.add_argument('--generate-rirs', action='store_true', help='明确生成此配置的新RIR；不生成混合PCM、不训练')
    args = parser.parse_args()
    state = load_config(args.config)
    if args.generate_rirs:
        print(str(generate_rirs(state, args.config)))
    elif args.export_geometry:
        output = new_run_directory(state, 'geometry')
        output.mkdir(parents=True)
        write_json(output / 'geometry.json', state)
        print(str(output / 'geometry.json'))
    else:
        print(json.dumps(state, ensure_ascii=False, indent=2, allow_nan=False))


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, FileNotFoundError) as exc:
        print('配置错误: ' + str(exc), file=sys.stderr)
        sys.exit(2)
