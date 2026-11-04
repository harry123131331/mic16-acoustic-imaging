"""TRANSPORT TEST FIXTURE: no detector, audio processing, or board execution."""
from pathlib import Path
import json
import sys
import time

request = Path(sys.argv[1])
job = request.parent
value = json.loads(request.read_text(encoding='utf-8'))
if value['pose_key'] == 'fixture_cancel':
    while not (job/'cancel.request').exists():
        time.sleep(.01)
    result = dict(schema='mic16.per_source.file_job.result.v1',status='CANCELED',error='fixture stopped')
    (job/'receipt.json').write_text(json.dumps(result),encoding='utf-8')
    raise SystemExit(2)
with (job/'all_cycles_fixture.jsonl').open('w',encoding='utf-8') as journal:
    for index in range(1000):
        event = dict(kind='progress',cycle=index+1,current_confirmed=[dict(source_id=1)],stats=dict(output_samples=index+1,peak_queued_items=1))
        journal.write(json.dumps(event)+'\n')
        print(json.dumps(event),flush=True)
(job/'audio').mkdir()
(job/'audio/session.json').write_text(json.dumps(dict(status='COMPLETED')),encoding='utf-8')
result = dict(schema='mic16.per_source.file_job.result.v1',status='COMPLETED',error='',
    statistics=dict(unprocessed_accepted_samples=0,completed_windows=1000,output_samples=1000))
(job/'receipt.json').write_text(json.dumps(result),encoding='utf-8')
