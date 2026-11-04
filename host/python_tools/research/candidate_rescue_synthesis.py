"""Bounded reproducible dry masks/history-aware mixture construction; no model."""
from pathlib import Path
import hashlib
import math
import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve, lfilter, resample_poly

FS=39062.5
N=40960
H=46875
BACKGROUND_RMS=.02*10**(-5/20)

def array_sha(x):return hashlib.sha256(np.asarray(x).tobytes()).hexdigest()
def rms(x):return float(np.sqrt(np.mean(np.asarray(x,dtype=float)**2)))

def dry_activity(x):
    """320-sample16k DC-removed block RMS, p95 reference, fixed -35dB proxy."""
    x=np.asarray(x,dtype=float)
    assert x.ndim==1 and np.isfinite(x).all() and np.any(x!=0)
    levels=[]
    for start in range(0,len(x),320):
        block=x[start:start+320];levels.append(rms(block-block.mean()))
    levels=np.array(levels)
    reference=float(np.percentile(levels,95))
    threshold=max(1e-6,reference*10**(-35/20))
    active=levels>=threshold
    mask=np.repeat(active,320)[:len(x)]
    return mask,dict(samples=len(x),block_rms=levels.tolist(),p95_rms=reference,threshold=threshold,
                     blocks_active=active.tolist(),active_samples=int(mask.sum()),
                     boundary='Dry energy proxy, not certified phonetic VAD; pauses retained')

def load_speech(rows):
    speech={};metadata={}
    for row in rows:
        x,rate=sf.read(row['path'],dtype='float32',always_2d=True)
        assert rate==16000 and x.shape[1]==1
        mask,detail=dry_activity(x[:,0])
        values=resample_poly(x[:,0],625,256).astype(np.float32)
        index=np.floor(np.arange(len(values))*256/625+.5).astype(int)
        resampled_mask=mask[np.minimum(index,len(mask)-1)]
        key=row['sha256']
        speech[key]=(values,resampled_mask)
        metadata[key]=dict(path=row['path'],role=row['role'],speaker_id=row['speaker_id'],source_sha256=key,
                           resampled_samples=len(values),resampled_audio_sha256=array_sha(values),
                           resampled_mask_sha256=array_sha(resampled_mask),**detail)
    return speech,metadata

def quiet_reference(seed):
    rng=np.random.default_rng(seed)
    x=lfilter([1.],[1.,-.8],rng.standard_normal((4*2048,16)),axis=0)
    return (x*(BACKGROUND_RMS/rms(x))).astype(np.float32).reshape(4,2048,16)

def schedule(recipe):
    """Center-aligned event times converted to dry timeline;5ms fade inside gate."""
    size=H+N;delay=recipe['center_direct_arrival_samples']
    start=max(0,round(H+recipe['center_on_s']*FS-delay))
    stop=min(size,round(H+recipe['center_off_s']*FS-delay))
    assert 0<=start<stop<=size
    gain=np.zeros(size,dtype=np.float32);gain[start:stop]=1
    fade=min(round(.005*FS),(stop-start)//2)
    if start>0 and fade:gain[start:start+fade]=np.linspace(0,1,fade,endpoint=False,dtype=np.float32)
    if stop<size and fade:gain[stop-fade:stop]=np.linspace(1,0,fade,endpoint=False,dtype=np.float32)
    return gain,dict(dry_start_sample=start,dry_stop_sample=stop,fade_samples=fade)

def build_scene(recipe,speech,rirs):
    """One ephemeral window; output components/masks used only for supervision audit."""
    rng=np.random.default_rng(recipe['background_seed'])
    noise=lfilter([1.],[1.,-.8],rng.standard_normal((N,16)),axis=0)
    background=noise*(BACKGROUND_RMS/rms(noise));mixture=background.copy()
    details=[];components=[];masks=[]
    for src in recipe['sources']:
        rir=rirs[src['rir_id']];size=H+N
        if src['role']=='speech':
            audio,mask=speech[src['source_sha256']];start=src['source_start_sample']
            dry=audio[start:start+size].copy();raw_mask=mask[start:start+size].copy()
            assert len(dry)==size
        else:
            nuisance_rng=np.random.default_rng(src['nuisance_seed']);time=np.arange(size)/FS
            if src['nuisance_kind']=='fan':
                dry=lfilter([1.],[1.,-.91],nuisance_rng.standard_normal(size))+.2*np.sin(2*np.pi*173*time)
            else:
                dry=np.sin(2*np.pi*230*time)+.35*np.sin(2*np.pi*460*time)+.08*nuisance_rng.standard_normal(size)
            raw_mask=np.ones(size,dtype=bool)
        event,info=schedule(src)
        # 使用未门控的完整窗参考定标，不能用短尾段。
        ungated=fftconvolve(dry[:,None],rir,mode='full',axes=0)[H:H+N]
        denominator=rms(ungated);assert denominator>1e-12
        scalar=BACKGROUND_RMS*10**((src['snr_db']+src['gain_db'])/20)/denominator
        if np.all(event==1):component=ungated*scalar
        else:component=fftconvolve((dry*event)[:,None],rir,mode='full',axes=0)[H:H+N]*scalar
        emitted=raw_mask & (event>0)
        indices=np.floor(H+np.arange(N)-src['center_direct_arrival_samples']+.5).astype(int)
        valid=(indices>=0)&(indices<size)
        active=np.zeros(N,dtype=bool);active[valid]=emitted[indices[valid]]
        detail=dict(**src,**info,active_samples=int(active.sum()),activity_mask_sha256=array_sha(active),
                    full_emitted_mask_sha256=array_sha(emitted),dry_audio_sha256=array_sha(dry),
                    unscheduled_reference_rms=denominator,applied_gain=float(scalar),
                    component_sha256=array_sha(component.astype(np.float32)),component_rms=rms(component),
                    actual_component_to_background_db=20*math.log10(max(rms(component),1e-30)/BACKGROUND_RMS),
                    final_frame_active_samples=int(active[-2048:].sum()))
        mixture+=component;details.append(detail);components.append(component.astype(np.float32));masks.append(active)
    pcm=mixture.astype(np.float32)
    assert pcm.shape==(N,16) and np.isfinite(pcm).all() and float(np.max(np.abs(pcm)))<1
    return pcm.reshape(20,2048,16),details,components,masks,dict(pcm_sha256=array_sha(pcm),pcm_rms=rms(pcm),
        pcm_peak_abs=float(np.max(np.abs(pcm))),background_sha256=array_sha(background.astype(np.float32)),
        background_rms=rms(background),temporal_noise_filter_axis=0,pcm_saved=False)
