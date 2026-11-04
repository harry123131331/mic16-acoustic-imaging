"""Proposed supervision-only occupancy contract, never a PCM/inference feature.

Support is counted from a registered dry source emission mask, shifted to the
array-center direct arrival. Reflected-tail energy must never extend that mask.
The mask's dry-energy proxy is not a certified phonetic VAD annotation.
"""
import math
import numpy as np
from acoustic_camera.valid_source_detection import angular_distance_deg
from research.candidate_rescue_features import assign_window_labels

VERSION="mic16_window_emission_supervision_200ms_v1_20261007"
SAMPLE_RATE=39062.5
WINDOW_SAMPLES=20*2048
MIN_SUPPORT_SECONDS=.2


def emission_state(active_samples,window_samples=WINDOW_SAMPLES,sample_rate=SAMPLE_RATE):
    if isinstance(active_samples,(bool,np.bool_)) or not isinstance(active_samples,(int,np.integer)):
        raise ValueError("active_samples must be an integer source-mask count")
    if not 0<=active_samples<=window_samples or window_samples<1 or not np.isfinite(sample_rate) or sample_rate<=0:
        raise ValueError("invalid window/count/rate")
    minimum=math.ceil(MIN_SUPPORT_SECONDS*sample_rate)
    if active_samples==0:return "OFF"
    if active_samples<minimum:return "GRAY_SHORT_EMISSION"
    return "ON_WINDOW_OCCUPANCY"


def label_candidate_window(directions,sources):
    """Sources: role, direction and direct-arrival-aligned dry active_samples.

    OFF sources (including pure reverb tails) create no positive or gray ring.
    Short nonzero emission is ignored locally within15deg; stable positives
    remain positive. All labels belong to the complete window, not last frame.
    """
    speech=[];nuisance=[];gray=[];states=[]
    for row in sources:
        if row["role"] not in ["speech","nuisance"]:raise ValueError("unknown source role")
        point=np.asarray(row["direction"],float)
        if point.shape!=(2,) or not np.isfinite(point).all() or np.any(np.abs(point)>=90):
            raise ValueError("source direction must be finite and front hemisphere")
        state=emission_state(row["active_samples"])
        states.append(dict(role=row["role"],direction=point.tolist(),active_samples=int(row["active_samples"]),state=state))
        if state=="ON_WINDOW_OCCUPANCY":
            (speech if row["role"]=="speech" else nuisance).append(point.tolist())
        elif state=="GRAY_SHORT_EMISSION":gray.append((row["role"],point))
    result=assign_window_labels(directions,speech,nuisance)
    spatial=np.array(result["spatial"],np.int8);valid=np.array(result["valid_speech"],np.int8)
    for c,direction in enumerate(np.asarray(directions,float)):
        for role,point in gray:
            if angular_distance_deg(*direction,*point)<=15:
                if spatial[c]!=1:spatial[c]=-1
                if role=="speech" and valid[c]!=1:valid[c]=-1
    result.update(spatial=spatial.tolist(),valid_speech=valid.tolist(),source_states=states,version=VERSION,
        positive_min_support_samples=math.ceil(MIN_SUPPORT_SECONDS*SAMPLE_RATE),window_samples=WINDOW_SAMPLES,
        label_kind="Registered dry-emission mask support >=200ms; 0=OFF; short emission ignored locally. Full-window occupancy, not instantaneous VAD or measured direct-path dominance.")
    return result
