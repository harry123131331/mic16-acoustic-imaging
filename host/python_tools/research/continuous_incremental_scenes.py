"""Two whole-stream TRAIN scenes; cut windows only after continuous convolution."""
from copy import deepcopy
import json
import math
from pathlib import Path
import numpy as np
from scipy.signal import fftconvolve,lfilter
from research.meeting_room_research_dataset import MeetingRoomResearchDataset,ROOT,SPEECH_LOCK
from research.candidate_rescue_synthesis import load_speech,rms,array_sha
from research.manual_room_simulation import digest
from research.spatial_diffuse_background import SpatialBackground

CONFIG=ROOT/'config/simulation/incremental_continuous16_20261014.json'


class ContinuousIncrementalScenes:
    def __init__(self):
        self.config=json.loads(CONFIG.read_text(encoding='utf-8'));p=self.config
        states=json.loads((ROOT/p['geometry_states']).read_text(encoding='utf-8'))
        self.states=[deepcopy(states[i]) for i in p['state_indices']]
        assert all(s['research_role']=='TRAIN' and len(s['sources'])==4 for s in self.states)
        self.fs=p['sample_rate_hz'];self.N=p['window_samples'];self.L=self.N*p['cycles_per_burst']
        self.rirs=None;self.rir_state=None;self.RIRs_generated=0;self.generated=set()
        rows=json.loads(SPEECH_LOCK.read_text(encoding='utf-8'))['speech_assets']
        eligible=[a for a in rows if a['role']=='TRAIN' and a['speaker_id'] not in {'116','700'} and a['duration_s']>=p['minimum_speech_asset_duration_s']]
        speakers=sorted({a['speaker_id'] for a in eligible},key=int);assert len(speakers)>=4
        self.assets={};self.recipes=[]
        for b,state in enumerate(self.states):
            sources=[]
            for slot,kind in enumerate(p['source_kinds_by_slot']):
                source=dict(slot=slot,kind=kind,seed=p['seed']+b*1000003+slot*100003)
                if kind=='speech':
                    speaker=speakers[2*b+int(slot==2)]
                    choices=sorted((a for a in eligible if a['speaker_id']==speaker),key=lambda a:a['archive_member'])
                    asset=choices[0];assert digest(Path(asset['path']))==asset['sha256']
                    self.assets[asset['sha256']]=asset;source.update(asset_sha256=asset['sha256'],original_path=asset['path'],speaker_id_audit_only=speaker)
                sources.append(source)
            self.recipes.append(dict(burst=b,state_index=b,sources=sources,background_seed=p['seed']+b*1000003+100000003,
                                     reference_seed=p['seed']+b*1000003+200000003))
        self.speech={};self.activity_metadata={};self.crop_audit=[]

    def prepare_crops(self):
        if self.speech:raise ValueError('one fixed dry selection only')
        self.speech,self.activity_metadata=load_speech(list(self.assets.values()))
        minimum=math.ceil(self.config['minimum_individual_and_joint_dry_activity_per_cycle_s']*self.fs)
        hop=round(self.config['crop_grid_s']*self.fs)
        for recipe,state in zip(self.recipes,self.states):
            H=state['sample_count'];size=H+self.L;common=np.ones(self.L,bool)
            for source in recipe['sources']:
                if source['kind']!='speech':continue
                audio,mask=self.speech[source['asset_sha256']];assert len(audio)>=size
                offset=math.floor(H-state['sources'][source['slot']]['distance_m']*self.fs/343.+.5)
                starts=np.arange(0,len(audio)-size+1,hop,dtype=np.int64);prefix=np.r_[0,np.cumsum(mask,dtype=np.int64)]
                count=np.stack([prefix[starts+offset+(c+1)*self.N]-prefix[starts+offset+c*self.N] for c in range(8)],1)
                eligible=np.flatnonzero((count>=minimum).all(1));joint=[]
                for j in eligible:
                    start=int(starts[j])+offset;direct=mask[start:start+self.L]
                    if ((direct&common).reshape(8,self.N).sum(1)>=minimum).all():joint.append(int(j))
                if not joint:raise ValueError('fixedasset has no eight-cycle joint-active continuouscrop;stop,noreplacement')
                chosen=joint[int(np.random.default_rng(source['seed']+80000003).integers(len(joint)))];crop=int(starts[chosen])
                direct=mask[crop+offset:crop+offset+self.L];common&=direct;source['crop_start_sample']=crop
                self.crop_audit.append(dict(burst=recipe['burst'],slot=source['slot'],asset_sha256=source['asset_sha256'],
                    candidate_crops=len(starts),eligible_crops=len(eligible),joint_eligible_crops=len(joint),crop_start_sample=crop,
                    per_cycle_active_samples=direct.reshape(8,self.N).sum(1).tolist(),direct_mask_sha256=array_sha(direct),noacoustic_scores_used=True))
            recipe['ungated_common_active_samples_per_cycle']=common.reshape(8,self.N).sum(1).tolist()

    def generate_state_rirs(self,index):
        if index not in (0,1):raise ValueError('onlytwoTRAIN geometries')
        if self.rir_state==index:return self.rirs
        if index in self.generated or self.RIRs_generated+4>8:raise ValueError('finite8RIR budget,no regeneration')
        result=MeetingRoomResearchDataset.generate_state_rirs(self,index);self.RIRs_generated+=len(result);self.generated.add(index)
        return result

    def synthesize(self,b):
        if b not in (0,1) or self.rir_state!=b:raise ValueError('verify correspondingRIRs before synthesis')
        state=self.states[b];recipe=self.recipes[b];H=state['sample_count'];size=H+self.L;p=self.config
        background_model=SpatialBackground(state['microphone_positions_m'])
        bg=background_model.generate(self.L,p['background_rms'],recipe['background_seed'])
        ref=background_model.generate(8192,p['background_rms'],recipe['reference_seed']).reshape(4,2048,16)
        total=bg.astype(float).copy();components=[];masks=[];details=[]
        for source in recipe['sources']:
            slot=source['slot'];kind=source['kind'];rng=np.random.default_rng(source['seed'])
            if kind=='speech':
                audio,mask=self.speech[source['asset_sha256']];crop=source['crop_start_sample']
                dry=audio[crop:crop+size].astype(float)*p['emitter_digital_rms']/rms(audio);raw_mask=mask[crop:crop+size]
            else:
                t=np.arange(size)/self.fs
                dry=lfilter([1.],[1.,-.91],rng.standard_normal(size))+.2*np.sin(2*np.pi*173*t) if kind=='fan' else np.sin(2*np.pi*230*t)+.35*np.sin(2*np.pi*460*t)+.08*rng.standard_normal(size)
                dry*=p['emitter_digital_rms']/rms(dry);raw_mask=np.ones(size,bool)
            delay=state['sources'][slot]['distance_m']*self.fs/343.;indices=np.floor(H+np.arange(self.L)-delay+.5).astype(int)
            assert indices.min()>=0 and indices.max()<size
            event=np.ones(size,float);border=math.floor(H+4*self.N-delay+.5);fade=round(p['fade_s']*self.fs)
            if b==1 and slot==0:
                event[:border]=0;event[border:border+fade]=np.linspace(0,1,fade,endpoint=False)
            if b==1 and slot==3:
                event[border:]=0;event[border-fade:border]=np.linspace(1,0,fade,endpoint=False)
            raw=fftconvolve(dry[:,None],self.rirs[slot],mode='full',axes=0)[H:H+self.L]
            scalar=rms(bg)*10**(p['ungated_burst_source_SBR_db']/20)/rms(raw)
            measured=20*math.log10(rms(raw*scalar)/rms(bg));assert abs(measured-15)<1e-9
            if np.all(event==1):comp=(raw*scalar).astype(np.float32)
            else:comp=(fftconvolve((dry*event)[:,None],self.rirs[slot],mode='full',axes=0)[H:H+self.L]*scalar).astype(np.float32)
            emitted=raw_mask&(event>0);active=emitted[indices];total+=comp;components.append(comp);masks.append(active)
            detail=dict(source_id=state['sources'][slot]['id'],source_slot=slot,direction=[state['sources'][slot]['azimuth_deg'],state['sources'][slot]['elevation_deg']],
                        position_m=state['sources'][slot]['position_m'],waveform_kind_audit_only=kind,ungated_reference_SBR_db=measured,
                        emitter_multiplier=scalar,continuous_component_sha256=array_sha(comp),continuous_activity_sha256=array_sha(active),
                        dry_audio_sha256=array_sha(dry.astype(np.float32)),gate_sha256=array_sha(event),
                        per_cycle_active_samples=active.reshape(8,self.N).sum(1).tolist())
            if kind=='speech':detail.update(original_asset_sha256=source['asset_sha256'],crop_start_sample=source['crop_start_sample'])
            details.append(detail);del raw,dry,event
        pcm=total.astype(np.float32);assert np.isfinite(pcm).all() and np.abs(pcm).max()<1
        recomputed=bg.astype(float).copy()
        for comp in components:recomputed+=comp
        assert np.array_equal(recomputed.astype(np.float32),pcm) and not np.shares_memory(pcm,ref)
        return dict(pcm=pcm,reference=ref,background=bg,components=components,activity_masks=masks,source_details=details,
                    metadata=dict(burst=b,continuous_whole_PCM_sha256=array_sha(pcm),reference_sha256=array_sha(ref),
                                  background_sha256=array_sha(bg),background_RMS=rms(bg),samples=self.L,duration_s=self.L/self.fs,
                                  PCM_peak_abs=float(np.max(np.abs(pcm))),whole_stream_generated_before_cutting=True,
                                  components_sum_exact=True,independent_quiet_reference=True,per_cycle_mixture_renormalization=False))

    def window(self,stream,b,c):
        if c not in range(8):raise ValueError('eight exactcycles only')
        begin=c*self.N;end=begin+self.N;source=[]
        for detail,mask in zip(stream['source_details'],stream['activity_masks']):
            current=deepcopy(detail);current['active_samples']=int(mask[begin:end].sum());current['activity_mask_sha256']=array_sha(mask[begin:end]);source.append(current)
        expected=4 if b==0 else 3
        assert sum(s['active_samples']>=math.ceil(.2*self.fs) for s in source)==expected
        assert all(s['active_samples']==0 or s['active_samples']>=math.ceil(.2*self.fs) for s in source)
        if b==1:
            inactive_slot=0 if c<4 else 3
            assert source[inactive_slot]['active_samples']==0
        return dict(frames=stream['pcm'][begin:end].reshape(20,2048,16),reference=stream['reference'],
                    record=dict(burst=b,cycle=c,role='TRAIN',source_details=source,pcm_sha256=array_sha(stream['pcm'][begin:end]),
                                reference_sha256=stream['metadata']['reference_sha256'],audio_start_s=begin/self.fs,audio_end_s=end/self.fs,
                                sample_start=begin,sample_end_exclusive=end,contiguous_within_burst=True))
