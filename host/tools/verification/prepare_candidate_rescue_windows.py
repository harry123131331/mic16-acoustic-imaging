"""Fixed320 ephemeral mixtures, truth-free88 features and separate occupancy labels."""
import os
os.environ['OPENBLAS_NUM_THREADS']='2'
os.environ['OMP_NUM_THREADS']='2'
os.environ['MKL_NUM_THREADS']='2'
from pathlib import Path
import hashlib
import json
import math
import sys
import time
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'python_tools'))
sys.path.insert(0,str(ROOT/'tools/verification'))
from research.candidate_rescue_synthesis import load_speech,quiet_reference,build_scene,array_sha,FS,N,H,BACKGROUND_RMS
from research.candidate_rescue_features import extract_candidate_features
from research.candidate_window_supervision import label_candidate_window
from acoustic_camera.tf_zone_histogram_2d import TfZoneProjector
from acoustic_camera.tf_zone_neural_features import extract_neural_features
from audit_pre_gate_candidate_coverage import clusters,fixed_bounded_pool,maximum_matching

OUT=ROOT/'results/candidate_window_preparation_20261008'
PRE=ROOT/'results/candidate_asset_preflight_20261007'
SPEECH=ROOT/'results/candidate_speech_intake_dev_other_20261007'
RIR=ROOT/'results/candidate_rir_preparation_20261007'
KINDS={'noise':(0,None),'fan':(0,'fan'),'motor':(0,'motor'),'pure_tail':(1,None),
       'one':(1,None),'two':(2,None),'three':(3,None),'one_fan':(1,'fan'),'two_fan':(2,'fan'),'transition':(1,None)}

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1024**2),b''):h.update(b)
 return h.hexdigest()
def save(name,value):
 with (OUT/name).open('x',encoding='utf-8') as f:json.dump(value,f,ensure_ascii=False,indent=2,allow_nan=False)

def plan_scenes(design,speech,rirs):
 plan=[];combinations=design['four_source_separated_combinations']
 for room_i,room in enumerate(design['rooms']):
  role=room['role'];ids=sorted({s['speaker_id'] for s in speech if s['role']==role},key=int)
  local_rirs={r['position_index']:r for r in rirs if r['room_id']==room['room_id']}
  local_index=0
  for kind,quota in design['windows_per_room'][role].items():
   for repeat in range(quota):
    index=len(plan);seed=20261008+1009*index;rng=np.random.default_rng(seed)
    count,nuisance=KINDS[kind];combo=combinations[(room_i*17+local_index*7)%len(combinations)]
    positions=list(rng.permutation(combo));sources=[]
    for j in range(count+int(nuisance is not None)):
     r=local_rirs[int(positions[j])];delay=r['center_direct_arrival_samples']
     source=dict(role='speech' if j<count else 'nuisance',rir_id=r['scene_id'],rir_sha256=r['sha256'],
       direction=[r['truth_azimuth_deg'],r['truth_elevation_deg']],center_direct_arrival_samples=delay,
       center_on_s=-1.2,center_off_s=N/FS+1,source_slot=j)
     if j<count:
      sid=ids[(room_i*5+local_index*3+j)%len(ids)]
      choices=sorted([s for s in speech if s['speaker_id']==sid],key=lambda s:s['archive_member'])
      chosen=choices[(local_index+j+room_i)%2]
      length=math.ceil(chosen['samples']*625/256);start=int(rng.integers(0,length-H-N+1))
      source.update(speaker_id=sid,source_sha256=chosen['sha256'],source_path=chosen['path'],source_start_sample=start,
       snr_db=float((0,5,10)[(local_index+j)%3]),gain_db=float(rng.uniform(-3,0)))
      if kind=='pure_tail':source['center_off_s']=-design['tail_offsets_after_center_stop_s'][role][repeat]
      elif kind=='transition':
       if repeat<quota//2:source.update(center_on_s=.20,center_off_s=.32,transition_design='SHORT_120MS')
       else:source.update(center_off_s=.60,transition_design='STOPPING_600MS')
     else:source.update(nuisance_kind=nuisance,nuisance_seed=seed+2000003+j,snr_db=0.,gain_db=0.)
     sources.append(source)
    plan.append(dict(scene_id=f'rescue_{role.lower()}_{room_i:02d}_{local_index:02d}_{kind}',index=index,role=role,
      room_id=room['room_id'],scenario=kind,repeat=repeat,scene_seed=seed,background_seed=seed+1000003,
      reference_index=room_i,sources=sources,pcm_storage='NONE_EPHEMERAL'))
    local_index+=1
 return plan

def votes_from_maps(projector,maps,frames,reference,allowed):
 power=np.stack([np.mean(np.abs(projector._frame_spectra(f))**2,axis=1) for f in frames])
 noise=np.stack([np.mean(np.abs(projector._frame_spectra(f))**2,axis=1) for f in reference])
 out=[]
 for t in range(20):
  for zi,z in enumerate(projector.zone_slices):
   score=maps[t,zi].reshape(-1);best=int(np.argmax(score));ei,ai=np.unravel_index(best,projector.grid.shape)
   level=10*np.log10(max(float(power[t,z].mean()),1e-20))
   floor=np.median(10*np.log10(np.maximum(noise[:,z].mean(1),1e-20)))
   center=float(projector.frequencies_hz[z].mean());second=float(np.max(score[allowed[best]]))
   out.append([t,zi,projector._coarse_band(center),center,float(projector.grid.azimuths_deg[ai]),
    float(projector.grid.elevations_deg[ei]),float(level-floor),float(score[best]),max(0.,float(score[best])-second)])
 return np.array(out,dtype=float)

def main():
 assert not OUT.exists(),'Stage already exists; no reroll/overwrite'
 design=read(PRE/'preparation_protocol.json');speech_lock=read(SPEECH/'speech_assets_lock.json')
 rir_lock=read(RIR/'rir_assets_lock.json');old_lock=read(RIR/'protocol_lock.json')
 assert read(RIR/'independent_verification.json')['status']=='PASS'
 assert read(SPEECH/'independent_verification.json')['status']=='PASS'
 assert all(sha(ROOT/p)==h for p,h in old_lock['protected_sha256'].items())
 assert all(sha(s['path'])==s['sha256'] for s in speech_lock['speech_assets'])
 assert all(sha(r['path'])==r['sha256'] for r in rir_lock['rir_assets'])
 plan=plan_scenes(design,speech_lock['speech_assets'],rir_lock['rir_assets'])
 assert len(plan)==320 and sum(s['role']=='TRAIN' for s in plan)==256
 inputs=[PRE/'preparation_protocol.json',SPEECH/'speech_assets_lock.json',SPEECH/'speaker_split.json',
  RIR/'rir_assets_lock.json',RIR/'protocol_lock.json',RIR/'independent_verification.json']
 sources=[Path(__file__),ROOT/'python_tools/research/candidate_rescue_synthesis.py',
  ROOT/'python_tools/research/candidate_rescue_features.py',ROOT/'python_tools/research/candidate_window_supervision.py',
  ROOT/'tools/verification/audit_pre_gate_candidate_coverage.py',ROOT/'python_tools/acoustic_camera/tf_zone_neural_features.py']
 OUT.mkdir()
 save('protocol_lock.json',dict(status='REGISTERED_BEFORE_MASKS_MIXTURES_OR_FEATURES',date='2026-10-08',
  inputs_sha256={str(p):sha(p) for p in inputs},source_sha256={str(p):sha(p) for p in sources},
  protected_sha256=old_lock['protected_sha256'],seed=20261008,scene_count=320,TRAIN=256,CAL=64,
  mask=dict(rate=16000,block=320,dc_removed=True,reference='p95 block RMS of original complete utterance',threshold='max(1e-6,p95*10^(-35/20))',
   resampling='nearest boolean source index floor(i*256/625+0.5); center shift nearest floor(H+i-delay+0.5)',
   schedule='Intersect actual dry mask and5ms faded explicit schedule; never extend for RIR energy',certified_vad=False),
  synthesis=dict(rate=FS,window_samples=N,rir_history_samples=H,foreground_snr_cycle_db=[0,5,10],gain_uniform_db=[-3,0],
   nuisance_to_background_db=0,background_rms=BACKGROUND_RMS,noise_filter_axis=0,
   source_audio_resampler='scipy resample_poly625/256 default; float32 same as prior corrected input',
   gain='Fixed per source from ungated full-window convolved RMS, applied to gated source; no tail renormalization',
   source_crop='One seed draw, natural pauses retained, no retries or candidate/label driven crop reselection'),
  proposal=dict(raw_all480_votes=True,fusion_deg=10,nms_deg=10,cap=16,ranking='fixed v1 lexicographic frames/bands/votes/positive weight'),
  coverage_gates=dict(CAL_overall_min=.90,each_CAL_room_min=.80,match_deg=10,ON_min_samples=7813,
   invalidity_stop='Any candidate gate failure stops this architecture; no cap/NMS search or additional training'),
  storage=dict(no_saved_pcm=True,no_speech_copies=True,feature_and_metadata_limit_bytes=64*1024**2),
  diagnostic_component_access='Labels/QA/plots only, never supplied to candidate/feature functions',
  network_implemented=False,training_runs=0,new_test_runs=0,formal_build_runs=0,
  runtime=dict(python=sys.version,numpy=np.__version__,blas_threads=2)))
 save('scene_plan.json',plan)
 save('plan_frozen.json',dict(scene_plan_sha256=sha(OUT/'scene_plan.json'),protocol_sha256=sha(OUT/'protocol_lock.json'),
  speaker_split_sha256=sha(SPEECH/'speaker_split.json'),source_choice_fixed_before_audio=True))
 speech,mask_meta=load_speech(speech_lock['speech_assets']);save('dry_activity_masks.json',mask_meta)
 rirs={}
 for row in rir_lock['rir_assets']:
  with np.load(row['path'],allow_pickle=False) as z:rirs[row['scene_id']]=z['rir'].copy()
 references=np.stack([quiet_reference(20261008+7000001+i*1009) for i in range(6)])
 projector=TfZoneProjector()
 aa,ee=np.meshgrid(projector.grid.azimuths_deg.astype(float),projector.grid.elevations_deg.astype(float))
 az,el=np.deg2rad(aa.ravel()),np.deg2rad(ee.ravel())
 cosine=np.sin(el[:,None])*np.sin(el[None,:])+np.cos(el[:,None])*np.cos(el[None,:])*np.cos(az[:,None]-az[None,:])
 allowed=np.rad2deg(np.arccos(np.clip(cosine,-1,1)))>=15
 features=np.zeros((320,16,20,88),np.float32);targets=np.full((320,16,2),-1,np.int8)
 valid=np.zeros((320,16),bool);directions=np.zeros((320,16,2),float)
 votes_all=np.zeros((320,480,9),float);packed_masks=np.zeros((320,4,N//8),np.uint8)
 records=[];started=time.monotonic();plot_data={}
 fixed_plot_ids=[12,15,60,63,262,263,286,287]
 for recipe in plan:
  i=recipe['index'];reference=references[recipe['reference_index']]
  frames,details,components,masks,pcm_info=build_scene(recipe,speech,rirs)
  maps,_=extract_neural_features(projector,frames,reference)
  votes=votes_from_maps(projector,maps,frames,reference,allowed)
  pool=fixed_bounded_pool(clusters(votes,np.ones(480,bool),10.));points=[c['direction'] for c in pool]
  assert 1<=len(points)<=16
  # 这些函数只接收PCM、参考、原始投票和方向，不接收声源元数据或掩模。
  tensor,info=extract_candidate_features(frames,reference,points,votes)
  supervision=label_candidate_window(points,details)
  count=len(points);features[i,:count]=tensor;valid[i,:count]=True;directions[i,:count]=points
  targets[i,:count,0]=supervision['spatial'];targets[i,:count,1]=supervision['valid_speech'];votes_all[i]=votes
  for slot,mask in enumerate(masks):packed_masks[i,slot]=np.packbits(mask,bitorder='little')
  ON=[d['direction'] for d in details if d['role']=='speech' and d['active_samples']>=7813]
  matching=maximum_matching(points,ON)
  records.append(dict(**recipe,source_details=details,**pcm_info,reference_sha256=array_sha(reference),
   candidate_count=count,candidate_directions=points,candidate_seed_directions=[c['seed'] for c in pool],
   supervision=supervision,ON_speech_directions=ON,coverage_pairs=matching,ON_speech_count=len(ON),
   covered_ON_speech=len(matching),feature_tensor_sha256=array_sha(tensor),
   labels_truth_separate=True,feature_timing_ms=info['timings_ms']))
  if i in fixed_plot_ids:
   power=np.mean(frames.reshape(N,16).astype(float)**2,axis=1)
   src_power=np.mean(components[0].astype(float)**2,axis=1) if components else np.zeros(N)
   plot_data[f'window_{i}_total_power']=power
   plot_data[f'window_{i}_speech_power']=src_power
   plot_data[f'window_{i}_dry_support']=masks[0] if masks else np.zeros(N,bool)
  if (i+1)%16==0:
   save_name=OUT/'progress.json'
   progress=dict(status='PREPARING_FIXED_WINDOWS',completed=i+1,budget=320,last=recipe['scene_id'],elapsed_seconds=time.monotonic()-started)
   save_name.write_text(json.dumps(progress,indent=2),encoding='utf-8');print(json.dumps(progress),flush=True)
 save('window_records.json',records)
 np.savez_compressed(OUT/'feature_cache.npz',features=features,labels=targets,candidate_valid=valid,directions=directions,
  votes=votes_all,source_activity_packbits=packed_masks,quiet_references=references)
 np.savez_compressed(OUT/'fixed_plot_data.npz',**plot_data)
 assert all(sha(ROOT/p)==h for p,h in old_lock['protected_sha256'].items())
 save('preparation.json',dict(status='320_WINDOWS_PREPARED_PENDING_INDEPENDENT_VALIDATION',scene_count=320,
  TRAIN=256,CAL=64,mixture_windows_constructed=320,pcm_files_saved=0,feature_cache_sha256=sha(OUT/'feature_cache.npz'),
  window_records_sha256=sha(OUT/'window_records.json'),elapsed_seconds=time.monotonic()-started,
  feature_shape=list(features.shape),feature_raw_bytes=features.nbytes,quiet_reference_count=6,
  training_runs=0,new_test_runs=0,formal_build_runs=0,network_implemented=False))
 assert sum(p.stat().st_size for p in OUT.iterdir() if p.is_file())<64*1024**2
 print('PREPARATION_FINISHED',flush=True)

if __name__=='__main__':
 try:main()
 except Exception as exc:
  if OUT.exists() and not (OUT/'preparation_failure.json').exists():save('preparation_failure.json',dict(status='STOP_NO_TRAINING',type=type(exc).__name__,error=str(exc)))
  raise
