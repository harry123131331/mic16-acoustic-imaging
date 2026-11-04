"""Explicit signed-int32 adapter matching the existing C live-input formula.

No default scale or channel permutation is inferred from peaks or RMS. A board
contract needs its own confirmed capture evidence; synthetic tests cannot be
used as real-device calibration.
"""
from pathlib import Path
import hashlib
import json
import re
import numpy as np


def file_sha(path):
    digest=hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda:stream.read(1048576),b''):digest.update(block)
    return digest.hexdigest()


class RawPcmContract:
    def __init__(self,path, *, input_origin):
        self.path=Path(path).resolve(strict=True)
        if self.path.stat().st_size>65536:raise ValueError('Raw PCM contract exceeds64KiB')
        value=json.loads(self.path.read_text(encoding='utf-8-sig'))
        required=dict(schema='mic16.raw_int32_contract.v1',sample_rate_hz=39062.5,
            container_bits=32,encoding='little_endian_twos_complement',layout='samples_channels',
            channel_count=16,right_shift_bits=0)
        if any(value.get(key)!=wanted for key,wanted in required.items()):
            raise ValueError('Unsupported raw PCM layout/rate/alignment contract')
        exponent=value.get('scale_exponent');mapping=value.get('logical_to_wire')
        if type(exponent) is not int or not 0<=exponent<=31:
            raise ValueError('Explicit confirmed scale exponent required; no board default')
        if not isinstance(mapping,list) or len(mapping)!=16 or any(type(x) is not int for x in mapping) or sorted(mapping)!=list(range(16)):
            raise ValueError('Explicit 16-channel permutation required')
        self.evidence_path=(self.path.parent/value['evidence_file']).resolve(strict=True)
        if self.evidence_path.stat().st_size>65536 or file_sha(self.evidence_path)!=value['evidence_sha256']:
            raise ValueError('Raw PCM evidence hash mismatch')
        evidence=json.loads(self.evidence_path.read_text(encoding='utf-8-sig'))
        if evidence.get('scale_exponent')!=exponent or evidence.get('logical_to_wire')!=mapping:
            raise ValueError('Evidence and scale/channel contract disagree')
        status=value.get('status')
        if status=='SYNTHETIC_TEST_ONLY':
            if input_origin!='SIMULATED_REPLAY' or evidence.get('scope')!='SYNTHETIC_TEST_VECTOR':
                raise ValueError('Synthetic scale cannot authorize captured board PCM')
        elif status=='CAPTURE_CONFIRMED':
            if (input_origin!='FILE_REPLAY' or evidence.get('scope')!='CONFIRMED_CAPTURE_CONTRACT' or
                    evidence.get('board_scale_verified') is not True or
                    evidence.get('physical_channel_mapping_verified') is not True or
                    not isinstance(value.get('firmware_sha256'),str) or not re.fullmatch(r'[0-9a-f]{64}',value['firmware_sha256']) or
                    evidence.get('firmware_sha256')!=value['firmware_sha256']):
                raise ValueError('Confirmed board scale/mapping/firmware evidence required')
        else:
            raise ValueError('Raw PCM contract is pending; do not guess scale')
        self.value=value;self.exponent=exponent;self.mapping=mapping
        self.dependencies=[self.path,self.evidence_path]

    def convert(self,raw):
        x=np.asarray(raw)
        if x.dtype!=np.dtype('<i4') or x.ndim!=2 or x.shape[1]!=16 or not 1<=len(x)<=8192:
            raise ValueError('Need bounded little-endian signed int32 samples×16')
        wide=x.astype(np.int64)
        bound=1<<self.exponent
        if np.any(wide>bound) or np.any(wide< -bound):
            raise ValueError('Raw PCM exceeds declared normalized range; no clipping')
        # 与C参考相同：有符号整数转FP64、相除、再转FP32。
        return np.ldexp(wide[:,self.mapping].astype(np.float64),-self.exponent).astype(np.float32)

    def receipt(self):
        return dict(schema='mic16.raw_int32_contract.v1',status=self.value['status'],
            scale_exponent=self.exponent,divisor=1<<self.exponent,logical_to_wire=self.mapping,
            amplitude_normalization='signed_counts / declared_power_of_two; no AGC or per-channel normalization',
            acoustic_SPL_calibrated=False,evidence_validation='DECLARED_CAPTURE_RECORD_HASH_AND_FIELDS_NOT_AN_INDEPENDENT_BOARD_MEASUREMENT')


class NormalizedRawView:
    """A read-only mapped input, converted only when the producer asks a chunk."""
    def __init__(self,array,contract):
        if array.dtype!=np.dtype('<i4') or array.ndim!=2 or array.shape[1]!=16:
            raise ValueError('Need little-endian int32 N×16 NPY')
        self.raw=array;self.contract=contract
    def __len__(self):return len(self.raw)
    def __getitem__(self,key):return self.contract.convert(self.raw[key])
