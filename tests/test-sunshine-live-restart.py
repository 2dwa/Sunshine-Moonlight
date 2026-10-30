# Contributor: GPT-6 Astra (AI-assisted), 2026-10-30. GPL-3.0.
import json,ssl,time,urllib.request
base='https://127.0.0.1:47990'
ctx=ssl._create_unverified_context()
def state():
 with urllib.request.urlopen(base+'/api/tray/state',context=ctx,timeout=2) as r: return json.load(r)
for i in range(2):
 before=state()
 assert not before.get('sessions'), 'Active streaming session; restart test cancelled'
 old=before['instance_id']
 req=urllib.request.Request(base+'/api/tray/action',data=b'{"action":"restart"}',headers={'Content-Type':'application/json'},method='POST')
 with urllib.request.urlopen(req,context=ctx,timeout=5) as r: assert json.load(r).get('status') is True
 deadline=time.monotonic()+25
 while time.monotonic()<deadline:
  time.sleep(.5)
  try:
   after=state()
   if after['instance_id']!=old:
    with urllib.request.urlopen(base+'/',context=ctx,timeout=3) as r: assert r.status==200
    print('PASS restart',i+1,': new backend instance, Web UI HTTP 200',flush=True)
    break
  except (OSError,ValueError): pass
 else: raise RuntimeError('Backend did not return after restart')
print('PASS: two successive standalone GUI-mode restarts',flush=True)
