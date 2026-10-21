#!/usr/bin/env python3
"""First actual two-queue runtime exploration; not full qualification."""
import sys, os, re, json, tempfile, uuid, traceback, csv, shutil, hashlib, signal, time, ctypes
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'Tools/research_sitl'))
from verify_stabilized_campaigns import Fixture
from pymavlink import mavutil
import argparse
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--evidence',type=Path,required=True)
parser.add_argument('--fault', choices=('manual_loss', 'wrong_source', 'estimator_loss', 'routing', 'process_stall', 'producer_stall', 'consumer_stall', 'drop_ack', 'delay_ack', 'drop_command', 'drop_grant', 'drop_disarm', 'replace_command_source', 'replace_ack_source'), default='manual_loss')
args=parser.parse_args()
build=args.evidence.resolve()
assert build.parent==Path('/var/tmp') and build.name.startswith('research-stabilized-sitl-')
receipt=build/'session-link-manifest.json'
manifest=json.loads(receipt.read_text())
for name,identity in manifest['files'].items():
 assert hashlib.sha256((build/name).read_bytes()).hexdigest()==identity, name
out=Path(tempfile.mkdtemp(prefix='research-stabilized-session-runtime-',dir='/var/tmp'))
print(out,flush=True)
(out/'sitl-lockstep-build').symlink_to(build/'sitl-lockstep-build',target_is_directory=True)
(out/'px4-source').symlink_to(build/'px4-source',target_is_directory=True)
(out/'plugin-build').mkdir()
(out/'plugin-build/libresearch_rotational_fixture.so').symlink_to(build/'session-plugin-build/libresearch_rotational_session.so')
# Test fixture schema with current synthetic tuning and selected MAVLink instance.
s=(ROOT/'src/lib/research_stabilized_manual/ObserverProfileTest.cpp').read_text().split('const char *fixture =',1)[1].split(';',1)[0]
tokens=''.join(re.findall(r'"([^"]*)"',s)).split()
assert len(tokens)==86
tokens[1]='0'
for axis,(inertia,damping,p,d) in enumerate(zip([.03,.08,.08],[.03,.04,.04],[.4,.8,.8],[.003,.006,.006])):
 inertia/=.4; damping/=.4
 tokens[35+axis]=str(.4*(damping+p-.4*(inertia+.05*damping+d)+.16*.05*inertia))
profile=out/'synthetic.profile'; profile.write_text(' '.join(tokens)+'\n')
namespace='/slaaps_session/'+uuid.uuid4().hex
class SessionFixture(Fixture):
 def client(self,module,*args,check=True):
  if module=='research_stabilized_sitl': module='research_session_sitl'
  return super().client(module,*args,check=check)
f=SessionFixture(out,'first-session',nose_up=True)
f.instance=29480
f.connection.close()
f.connection=mavutil.mavlink_connection('udpout:127.0.0.1:19785',source_system=255,source_component=190)
s=f.world.read_text().replace('research_stabilized_fixture::RotationalFixture','research_stabilized_fixture::RotationalSessionFixture').replace('<truth_csv>','<session_namespace>'+namespace+'</session_namespace>\n        <truth_csv>')
f.world.write_text(s)
s=f.script.read_text().replace('param set COM_RC_IN_MODE 1','param set COM_RC_IN_MODE 1\nparam set COM_PREARM_MODE 2').replace('19779','19785').replace('19780','19786')
s=s.replace('research_stabilized_sitl start slaaps_fixture','research_session_sitl start '+str(profile)+' '+namespace)
f.script.write_text(s)
relay=None
if args.fault in ('drop_ack','delay_ack','drop_command', 'drop_grant', 'drop_disarm'):
 from session_transport_relay import SessionTransportRelay
 plant_namespace='/slaaps_session/'+uuid.uuid4().hex
 f.world.write_text(f.world.read_text().replace(namespace,plant_namespace))
 relay=SessionTransportRelay(f.env['GZ_PARTITION'],namespace,plant_namespace,f.work/'transport-relay.jsonl')
 shutil.copy2(ROOT/'Tools/research_sitl/session_transport_relay.py',out/'session_transport_relay.py')

result={'build_manifest_sha256':hashlib.sha256(receipt.read_bytes()).hexdigest(),'scope':'two_queue_fault_smoke','fault_case':args.fault,'passed':False,'build':str(build),'namespace':namespace,'work':str(f.work)}
try:
 f.start()
 output,code=f.client('param','compare','COM_PREARM_MODE','2',check=False)
 assert code==0,output
 result['initial_status']=f.client('research_session_sitl','status')[0]
 f.command=(0,200,0); f.action('roll',200); f.wait(1000000)
 f.command=(0,0,0); f.action('release'); f.wait(3000000)
 result['hold_status']=f.client('research_session_sitl','status')[0]
 assert 'state=3' in result['hold_status'] and 'fault=0' in result['hold_status'],result['hold_status']
 assert int(re.search(r'acknowledgements=(\d+)',result['hold_status']).group(1))>10
 f.action('inject',args.fault)
 result['injection_us']=f.now()
 if relay is not None:
  relay.set_mode(args.fault)
  if args.fault=='drop_disarm':
   f.sending=False
  elif args.fault=='drop_grant':
   f.client('research_session_sitl','disable'); f.wait(300000)
   result['lost_grant_request_us']=f.now()
   f.client('research_session_sitl','restart'); f.wait(500000)
   result['grant_pending_status']=f.client('research_session_sitl','status')[0]
   assert 'phase=1' in result['grant_pending_status'], 'Expected unconfirmed grant'
   f.wait(2000000)

 elif args.fault in ('replace_command_source','replace_ack_source'):
  f.client('research_session_sitl',args.fault)
 elif args.fault=='manual_loss':
  f.sending=False
 elif args.fault=='wrong_source':
  f.sending=False
  wrong=mavutil.mavlink_connection('udpout:127.0.0.1:19785',source_system=254,source_component=190)
  try:
   target=f.now()+300000
   deadline=time.monotonic()+10
   while f.now()<target:
    assert time.monotonic()<deadline, 'Wrong-source injection stopped advancing'
    wrong.mav.manual_control_send(1,0,0,500,0,0)
    time.sleep(.02)
  finally: wrong.close()
 elif args.fault=='estimator_loss':
  f.client('ekf2','stop')
 elif args.fault=='routing':
  f.client('param','set','SIM_GZ_SV_FUNC1','201')
 elif args.fault in ('producer_stall','consumer_stall'):
  # Linux PTRACE_SEIZE/INTERRUPT stops only the selected work-queue thread.
  # SIGSTOP would stop every queue and would not prove independent scheduling.
  expected=('wq:slaaps_prod' if args.fault=='producer_stall' else 'wq:slaaps_cons')
  matches=[p for p in Path(f'/proc/{f.px4.pid}/task').iterdir() if (p/'comm').read_text().strip()==expected]
  assert len(matches)==1, 'Expected exactly one matching queue thread'
  tid=int(matches[0].name)
  libc=ctypes.CDLL(None,use_errno=True)
  libc.ptrace.argtypes=[ctypes.c_ulong,ctypes.c_int,ctypes.c_void_p,ctypes.c_void_p]
  libc.ptrace.restype=ctypes.c_long
  def ptrace(request):
   if libc.ptrace(request,tid,None,None)==-1: raise OSError(ctypes.get_errno(),os.strerror(ctypes.get_errno()))
  ptrace(0x4206)  # PTRACE_SEIZE
  try:
   ptrace(0x4207)  # PTRACE_INTERRUPT
   deadline=time.monotonic()+5
   while True:
    waited,status=os.waitpid(tid,0x40000000|os.WNOHANG)  # __WALL
    if waited:
     assert os.WIFSTOPPED(status), 'Queue did not enter ptrace stop'
     break
    assert time.monotonic()<deadline, 'Queue stop timed out'
    time.sleep(.005)
   result['stalled_thread']={'tid':tid,'comm':expected,'wait_status':status}
   result['queue_stall_start_us']=f.now()
   f.wait(500000)
   result['during_queue_stall_status']=f.client('research_session_sitl','status')[0]
   result['queue_stall_end_us']=f.now()
   during=dict(re.findall(r'(phase|state|consumer|commands|acknowledgements|fault)=(\d+)',result['during_queue_stall_status']))
   if args.fault=='producer_stall':
    assert during['consumer']=='5', 'Consumer did not independently fault while producer was stopped'
   else:
    assert during['phase']=='3' and during['state']=='4' and during['consumer']=='3', 'Producer did not independently fault while consumer was stopped'
   assert result['queue_stall_end_us']-result['queue_stall_start_us']>=500000

  finally:
   ptrace(17)  # PTRACE_DETACH resumes this thread.
 elif args.fault=='process_stall':
  f.px4.send_signal(signal.SIGSTOP); f.paused=True
  f.wait(500000)
  f.px4.send_signal(signal.SIGCONT); f.paused=False
 f.wait(1000000)
 result['loss_status']=f.client('research_session_sitl','status')[0]
 assert 'phase=3' in result['loss_status'] and 'fault=0' not in result['loss_status'],result['loss_status']
 output,code=f.client('research_session_sitl','enable',check=False)
 assert code!=0, 'Fault recovered through enable instead of explicit restart'
 result['reenable_rejected']=True
 if args.fault=='drop_disarm':
  assert 'consumer=4' in result['loss_status'], 'Consumer did not retain disarm barrier'
  output,code=f.client('research_session_sitl','restart',check=False)
  assert code!=0, 'Restart accepted before matching plant disarm reply'
  result['restart_without_disarm_rejected']=True

 if args.fault=='estimator_loss':
  f.client('ekf2','start')
  f.wait(5000000)
 if args.fault=='routing': f.client('param','set','SIM_GZ_SV_FUNC1','0')
 if relay is not None: relay.set_mode('pass')
 f.sending=True; f.wait(500000)
 status=f.client('research_session_sitl','status')[0]
 assert 'phase=3' in status and 'state=4' in status, 'Recovery automatically reenabled outputs'
 result['restart_request_us']=f.now()
 f.action('restart_request'); f.client('research_session_sitl','restart'); f.wait(1000000)
 result['restart_status']=f.client('research_session_sitl','status')[0]
 assert 'state=3' in result['restart_status'] and 'fault=0' in result['restart_status'],result['restart_status']
 f.action('permission_loss'); f.client('param','set','COM_PREARM_MODE','0'); f.wait(1000000)
 result['permission_loss_status']=f.client('research_session_sitl','status')[0]
 assert 'phase=3' in result['permission_loss_status'] and 'fault=0' not in result['permission_loss_status'],result['permission_loss_status']
 result['passed']=True
except Exception:
 result['error']=traceback.format_exc(); print(result['error'],flush=True)
 if f.px4 is not None and f.px4.poll() is None:
  for module,args in [('research_session_sitl',['status']),('listener',['actuator_armed','-n','1']),('listener',['vehicle_attitude','-n','1']),('listener',['manual_control_setpoint','-n','1'])]:
   try: (f.work/(module+'-'+args[0]+'.log')).write_text(f.client(module,*args,check=False)[0])
   except Exception: pass
finally:
 f.close()
 if relay is not None: relay.close()
 try:
  if result['passed']:
   rows=list(csv.DictReader((f.work/'truth.csv').open()))
   enabled=[r for r in rows if r['enabled']=='1']
   assert enabled and all(float(r['thrust'])==10 for r in enabled)
   assert len({r['session'] for r in enabled})==2
   assert max(float(r['wx']) for r in enabled)>.05, 'No meaningful roll response'
   off=[r for r in rows if result['injection_us']+300000<=int(r['time'])<result['restart_request_us']]
   assert off and all(r['enabled']=='0' and float(r['thrust'])==0 and all(float(r['t'+str(i)])==0 for i in range(4)) for r in off), 'Fault did not sustain plant disarm'
   if args.fault in ('process_stall','consumer_stall'):
    first_off=next(r for r in rows if int(r['time'])>result['injection_us'] and r['enabled']=='0')
    expiry=int(first_off['time'])-int(first_off['command_time'])
    assert 100000<=expiry<=104000, 'Plant lease did not expire within one simulation step'
    result['plant_expiry_delay_us']=expiry
   assert rows[-1]['enabled']=='0' and float(rows[-1]['thrust'])==0
   assert all(float(rows[-1]['t'+str(i)])==0 for i in range(4))
   if relay is not None:
    events=[json.loads(line) for line in (f.work/'transport-relay.jsonl').read_text().splitlines()]
    injected=[e for e in events if e['action']==('delay' if args.fault=='delay_ack' else 'drop') and e['kind']==('reply' if args.fault in ('drop_grant','drop_disarm') else 'command' if args.fault=='drop_command' else 'ack')]
    assert injected, 'No transport fault was injected'
    if args.fault in ('drop_grant','drop_disarm'):
     expected_kind=6 if args.fault=='drop_grant' else 7
     assert all(int(e['data'][1])==expected_kind for e in injected)
    if args.fault=='drop_grant':
     request_time=result['lost_grant_request_us']
     assert not any(e['kind']=='command' and e['action']=='receive' and request_time<=e['sim_time_us']<result['restart_request_us'] for e in events), 'Commands published before grant confirmation'

    if args.fault=='delay_ack':
     released=[e for e in events if e['action']=='release' and e['kind']=='ack']
     assert released and all(e['sim_time_us']-int(e['data'][6])>=80000 for e in released)
    result['transport_injected_messages']=len(injected)
   if args.fault in ('replace_command_source','replace_ack_source'):
    kind='command' if args.fault=='replace_command_source' else 'ack'
    match=re.search(r'lifecycle '+kind+r' ok=1 before=(\d+) after=(\d+) elapsed=(\d+) age=(\d+)',(f.work/'px4.log').read_text())
    assert match, 'No successful identical-payload publisher replacement'
    old,new,elapsed,age=map(int,match.groups())
    assert old!=new and elapsed+age<50000, 'Replacement could be confused with completion expiry'
    result['lifecycle_replacement']={'before':old,'after':new,'elapsed_us':elapsed,'payload_age_us':age}
   result['plant_checks']={'sessions':2,'fixed_thrust_n':10,'peak_roll_rate_rad_s':max(float(r['wx']) for r in enabled),'final_targets_zero':True}
 except Exception:
  result['passed']=False
  result['error']=traceback.format_exc()
 shutil.copy2(__file__,out/Path(__file__).name)
 (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps(result,indent=2),flush=True)

files={str(p.relative_to(out)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file() and not p.is_symlink() and not any(part in ('sitl-lockstep-build','px4-source') for part in p.relative_to(out).parts)}
(out/'runtime-manifest.json').write_text(json.dumps({'scope':'two_queue_runtime_smoke_only','files':files},indent=2)+'\n')
print('receipt',hashlib.sha256((out/'runtime-manifest.json').read_bytes()).hexdigest(),flush=True)

sys.exit(0 if result['passed'] else 1)
