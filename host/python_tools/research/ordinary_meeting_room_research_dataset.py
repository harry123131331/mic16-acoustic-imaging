"""Role-isolated ordinary-room recipes and RAM-only acoustic synthesis.

Dry activity selects ordinary windows before RIR/feature/model computation.
Receiver SBR controls source-specific emitter gains, never mixture gain.
"""
from copy import deepcopy
import math
import numpy as np
from scipy.signal import fftconvolve,lfilter
from research.meeting_room_research_dataset import MeetingRoomResearchDataset,ROOT
from research.meeting_room_ordinary_dataset import OrdinaryMeetingRoomDataset
from research.candidate_rescue_synthesis import rms,array_sha

CONFIG=ROOT/'config/simulation/meeting_room_ordinary_research_20261012.json'


class OrdinaryRoomResearchDataset(OrdinaryMeetingRoomDataset):
    def __init__(self,config_path=CONFIG):
        # 共用声源、RIR及背景实现保持不变。
        MeetingRoomResearchDataset.__init__(self,config_path)
        self.crop_audit={};self.crops_ready=set();p=self.config
        patterns=p['ordinary']['per_pose_kind_patterns']
        assert len(patterns)==16 and sum(not x for x in patterns)==4
        assert sum(len(x)==1 for x in patterns)==sum(len(x)==2 for x in patterns)==6
        for recipe in self.plan:
            index=recipe['index'];local=index%16;state=recipe['state_index'];group=state//2
            kinds=patterns[local];recipe['configured_emitting_points']=len(kinds)
            recipe['scene_id']=f'ordinary_{recipe["role"].lower()}_g{group:02d}_p{recipe["pose_index"]}_w{local:02d}_K{len(kinds)}'
            recipe['source_recipes']=[]
            assets=self.asset_groups[recipe['role']];speakers=sorted({a['speaker_id'] for a in assets})
            slots=[(local+state)%2] if len(kinds)==1 else list(range(len(kinds)))
            target=p['ordinary']['receiver_window_SBR_db'][(local+state)%3]
            for j,(slot,kind) in enumerate(zip(slots,kinds)):
                seed=p['seed']+1009*(index+1)+slot*100003
                src=dict(slot=slot,kind=kind,seed=seed,gain_db=0.,
                    target_receiver_window_SBR_db=float(target-(5 if target==20 and j==1 else 0)))
                if kind=='speech':
                    speaker=speakers[(group*3+local+j)%len(speakers)]
                    subset=[a for a in assets if a['speaker_id']==speaker]
                    asset=subset[(state+local+j)%len(subset)]
                    src.update(asset_sha256=asset['sha256'],asset_path=asset['path'],speaker_id_audit_only=speaker,
                        crop_seed=seed+80000003)
                recipe['source_recipes'].append(src)
        assert len(self.plan)==320 and len(self.states)==20

    def prepare_source_crops(self,role):
        if role=='RESERVE':
            raise ValueError('RESERVE dry audio/crops remain sealed')
        if role in self.crops_ready:
            return self.crop_audit[role]
        self.prepare_audio(role);audit=[];p=self.config['ordinary']
        minimum=math.ceil(p['minimum_active_duration_s']*self.fs)
        hop=max(1,round(p['speech_crop_grid_s']*self.fs));prefixes={}
        for recipe in self.plan:
            if recipe['role']!=role:continue
            state=self.states[recipe['state_index']];H=state['sample_count'];size=H+self.N
            for src in recipe['source_recipes']:
                if src['kind']!='speech':continue
                sha=src['asset_sha256'];audio,mask=self.speech[sha]
                assert len(audio)>=size,'original speech asset too short; stop'
                if sha not in prefixes:
                    prefixes[sha]=np.r_[0,np.cumsum(mask,dtype=np.int64)]
                delay=state['sources'][src['slot']]['distance_m']*self.fs/343.
                offset=math.floor(H-delay+.5)
                candidates=np.arange(0,len(audio)-size+1,hop,dtype=np.int64)
                counts=prefixes[sha][candidates+offset+self.N]-prefixes[sha][candidates+offset]
                eligible=np.flatnonzero(counts>=minimum)
                assert len(eligible)>0,'no ordinary active crop; stop, do not replace asset'
                selected=int(eligible[int(np.random.default_rng(src['crop_seed']).integers(len(eligible)))])
                crop=int(candidates[selected]);count=int(counts[selected])
                # 通过独立直接索引核对，不沿用前缀实现。
                direct=mask[crop+offset:crop+offset+self.N]
                assert len(direct)==self.N and int(direct.sum())==count and count>=minimum
                src['crop_start_sample']=crop;src['expected_active_samples']=count
                audit.append(dict(index=recipe['index'],source_slot=src['slot'],asset_sha256=sha,
                    speaker_id_audit_only=src['speaker_id_audit_only'],candidate_crop_count=len(candidates),
                    eligible_crop_count=len(eligible),crop_start_sample=crop,expected_active_samples=count,
                    activity_mask_sha256=array_sha(direct),crop_seed=src['crop_seed']))
        self.crop_audit[role]=audit;self.crops_ready.add(role)
        return audit

    def sample(self,index):
        recipe=self.plan[index];role=recipe['role']
        if role=='RESERVE':raise ValueError('RESERVE acoustic generation remains sealed')
        self.prepare_source_crops(role);state=self.states[recipe['state_index']]
        if self.rir_state!=recipe['state_index']:self.generate_state_rirs(recipe['state_index'])
        H=state['sample_count'];size=H+self.N
        bg=self.background.generate(self.N,recipe['background_rms'],recipe['background_seed'])
        ref=self.background.generate(8192,recipe['background_rms'],recipe['reference_seed']).reshape(4,2048,16)
        pcm=bg.astype(float);components=[];masks=[];details=[]
        for src in recipe['source_recipes']:
            slot=src['slot'];kind=src['kind'];extra={};rng=np.random.default_rng(src['seed'])
            if kind=='speech':
                audio,mask=self.speech[src['asset_sha256']];crop=src['crop_start_sample']
                dry=audio[crop:crop+size].astype(float)*self.config['emitter_digital_rms']/rms(audio)
                emitted=mask[crop:crop+size]
                extra=dict(original_asset_sha256=src['asset_sha256'],crop_start_sample=crop)
            else:
                t=np.arange(size)/self.fs
                if kind=='fan':dry=lfilter([1.],[1.,-.91],rng.standard_normal(size))+.2*np.sin(2*np.pi*173*t)
                elif kind=='motor':dry=np.sin(2*np.pi*230*t)+.35*np.sin(2*np.pi*460*t)+.08*rng.standard_normal(size)
                else:raise ValueError('unsupported waveform recipe')
                dry*=self.config['emitter_digital_rms']/rms(dry);emitted=np.ones(size,bool)
            raw=fftconvolve(dry[:,None],self.rirs[slot],mode='full',axes=0)[H:H+self.N].astype(np.float32)
            target=src['target_receiver_window_SBR_db']
            factor=rms(bg)*10**(target/20)/rms(raw)
            comp=(raw.astype(float)*factor).astype(np.float32)
            measured=20*math.log10(rms(comp)/rms(bg));assert abs(measured-target)<1e-5
            source=state['sources'][slot];delay=source['distance_m']*self.fs/343.
            ids=np.floor(H+np.arange(self.N)-delay+.5).astype(int)
            assert (ids>=0).all() and (ids<size).all()
            active=emitted[ids]
            assert int(active.sum())>=math.ceil(.2*self.fs)
            if kind=='speech':assert int(active.sum())==src['expected_active_samples']
            pcm+=comp;components.append(comp);masks.append(active)
            details.append(dict(source_id=source['id'],source_slot=slot,direction=[source['azimuth_deg'],source['elevation_deg']],
                position_m=source['position_m'],distance_m=source['distance_m'],active_samples=int(active.sum()),
                activity_mask_sha256=array_sha(active),component_sha256=array_sha(comp),component_rms=rms(comp),
                component_to_background_db=measured,target_receiver_window_SBR_db=target,
                additional_emitter_gain_db=20*math.log10(factor),emitter_gain_multiplier=factor,
                waveform_kind_audit_only=kind,**extra))
        pcm=pcm.astype(np.float32)
        assert np.isfinite(pcm).all() and np.max(np.abs(pcm))<1,'clipping: stop, no reroll/mixture normalization'
        return dict(frames=pcm.reshape(20,2048,16),reference=ref,background=bg,
            components=components,activity_masks=masks,record=dict(**deepcopy(recipe),source_details=details,
                pcm_sha256=array_sha(pcm),reference_sha256=array_sha(ref),pcm_peak_abs=float(np.max(np.abs(pcm))),
                pcm_saved=False,receiver_level_renormalized=False,per_source_receiver_SBR_control=True,
                actual_background_model=self.config['background']['model'],protocol_version='ordinary_research_20261012'))
