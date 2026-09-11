"""Experimental single-process DMC loop. Runtime pad channel or verified window input."""
import argparse, ctypes as c, hashlib, json, os, subprocess, time
from ctypes import wintypes as w
from pathlib import Path
from PIL import ImageGrab

ROOT=Path(__file__).resolve().parents[3]
ap=argparse.ArgumentParser()
ap.add_argument('--runs',type=int,default=1)
ap.add_argument('--seconds',type=int,default=180)
ap.add_argument('--boot-timeout',type=int,default=30)
ap.add_argument('--menu-timeout',type=int,default=45)
ap.add_argument('--pss-timeout',type=int,default=120)
ap.add_argument('--target-timeout',type=int,default=30)
ap.add_argument('--confirm',action='store_true',help='Bounded X pulses to verified process window; use only for boot discovery')
ap.add_argument('--keys',default='ENTER,X',help='Finite comma-separated sequence, used with --confirm')
ap.add_argument('--method',choices=['message','sendinput','runtime'],default='message')
ap.add_argument('--target',default='[GP:H]')
ap.add_argument('--diagnostics',action='store_true')
ap.add_argument('--post-target',type=int,default=0,help='Observation seconds after target; no more inputs')
ap.add_argument('--try-movie-skip',action='store_true',help='One Start pulse 5s after the observed GP:H stall, runtime channel only')
args=ap.parse_args()
if args.runs<1 or args.seconds<1:ap.error('runs and seconds must be positive')
if args.try_movie_skip and (args.method!='runtime' or args.post_target<15 or args.target!='[GP:H]'):
    ap.error('Movie skip requires runtime method, GP:H target, and post-target >=15')
keys=args.keys.split(',')
keymap={'ENTER':(0x0d,0x1c),'X':(0x58,0x2d),'C':(0x43,0x2e)}
assert all(k in keymap for k in keys)
u=c.WinDLL('user32',use_last_error=True)
u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
u.IsWindowVisible.argtypes=[w.HWND]
u.GetWindowTextW.argtypes=[w.HWND,w.LPWSTR,c.c_int]
u.SetForegroundWindow.argtypes=[w.HWND]
u.GetForegroundWindow.restype=w.HWND
class KI(c.Structure):
    _fields_=[('vk',w.WORD),('scan',w.WORD),('flags',w.DWORD),('time',w.DWORD),('extra',c.c_size_t)]
class MI(c.Structure):
    _fields_=[('x',w.LONG),('y',w.LONG),('data',w.DWORD),('flags',w.DWORD),('time',w.DWORD),('extra',c.c_size_t)]
class IU(c.Union):_fields_=[('ki',KI),('mi',MI)]
class INPUT(c.Structure):_fields_=[('type',w.DWORD),('value',IU)]
def send_key(hwnd,vk,scan,up=False):
    if u.GetForegroundWindow()!=hwnd:raise RuntimeError('Target lost foreground; input stopped')
    inp=INPUT(type=1,value=IU(ki=KI(vk,scan,2 if up else 0,0,0)))
    return u.SendInput(1,c.byref(inp),c.sizeof(inp))==1
CB=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
def windows(pid):
    found=[]
    @CB
    def cb(hwnd,_):
        owner=w.DWORD(); u.GetWindowThreadProcessId(hwnd,c.byref(owner))
        if owner.value==pid and u.IsWindowVisible(hwnd):
            title=c.create_unicode_buffer(512);u.GetWindowTextW(hwnd,title,512)
            found.append(dict(hwnd=int(hwnd),title=title.value))
        return True
    u.EnumWindows(cb,0)
    return found

exe=ROOT/'analysis/local/symtabfirst/build/bin/Debug/dmc-recomp.exe'
_iso_env=os.environ.get('PS2X_CD_IMAGE')
assert _iso_env,'Set PS2X_CD_IMAGE to your own DMC disc image path before running this loop (see README: bring your own ELF/disc image).'
iso=Path(_iso_env)
elf=ROOT/'original/SLES_503.58'
assert iso.is_file() and exe.is_file()
assert hashlib.sha256(elf.read_bytes()).hexdigest()=='d0753a6b3b2f00802a50758a872d8cf051725aa31c839aa8894eee30ce58bab4'
env=os.environ.copy()
env.update(PS2X_CD_IMAGE=str(iso),PS2X_RUNTIME_ARENA_BASE='0x009FA000',PS2X_RUNTIME_ARENA_LIMIT='0x00ABE000')
out=ROOT/'analysis/local/p311';out.mkdir(exist_ok=True)
# Exclusive harness lock prevents overlapping runs. Stale lock is deliberately not deleted automatically.
lock=out/'loop.lock'
fd=os.open(lock,os.O_CREAT|os.O_EXCL|os.O_WRONLY)
os.write(fd,str(os.getpid()).encode());os.close(fd)
try:
    for _ in range(args.runs):
        n=1
        while (out/f'RUN_{n:03d}').exists():n+=1
        d=out/f'RUN_{n:03d}';d.mkdir()
        padfile=d/'pad.txt';padfile.write_text('FFFF')
        if args.method=='runtime':env['DMC_P311_PAD_FILE']=str(padfile)
        else:env.pop('DMC_P311_PAD_FILE',None)
        for k in ('DMC_P311_MPEG_DIAG','DMC_P311_RAM_PREINIT','DMC_P311_RAM_GETPICTURE'):env.pop(k,None)
        if args.diagnostics:env.update(DMC_P311_MPEG_DIAG='1',DMC_P311_RAM_PREINIT=str(d/'preinit.bin'),DMC_P311_RAM_GETPICTURE=str(d/'getpicture.bin'))
        meta=dict(run=d.name,timestamp=time.strftime('%Y-%m-%dT%H:%M:%S%z'),env={k:env[k] for k in ('PS2X_CD_IMAGE','PS2X_RUNTIME_ARENA_BASE','PS2X_RUNTIME_ARENA_LIMIT')},inputs=[],windows=[],milestones={},result='BOOT_ONLY',exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest())
        meta['diagnostic_env']={k:v for k,v in env.items() if k.startswith('DMC_P311_')}
        log=d/'runtime.log';proc=None;t=time.monotonic();last=0; captured=False; capture_step=0;skip_sent=False
        try:
            with log.open('wb') as f:
                existing=subprocess.check_output(['powershell','-NoProfile','-Command',"@(Get-Process -Name 'dmc-recomp' -ErrorAction SilentlyContinue).Id | ConvertTo-Json -Compress"],creationflags=subprocess.CREATE_NO_WINDOW,text=True).strip()
                if existing:raise RuntimeError('Pre-existing RECOMP process; refusing overlapping launch: '+existing)
                proc=subprocess.Popen([str(exe),str(elf)],cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT)
                meta['pid']=proc.pid
                print(json.dumps(dict(run=d.name,pid=proc.pid,log=str(log))),flush=True)
                while time.monotonic()-t<args.seconds:
                    elapsed=time.monotonic()-t
                    text=log.read_text(errors='replace')
                    for name,marker in [('MC_CHECK','[MC] GetDir'),('MOVIE_START','[CDMODULE/MOVIE:start]'),('PSS_REACHED','[MPEG:diag] feed call'),('MPEG_INIT','[MPEG:init]'),('TARGET',args.target)]:
                        if marker in text and name not in meta['milestones']:meta['milestones'][name]=elapsed
                    wins=windows(proc.pid)
                    if wins!=meta['windows']:
                        meta['windows']=wins;print(json.dumps(dict(run=d.name,windows=wins)),flush=True)
                    if elapsed>=8 and len(wins)==1 and not captured:
                        try:
                            ImageGrab.grab(window=wins[0]['hwnd']).save(d/'boot.png')
                        except Exception as e:meta['capture_error']=repr(e)
                        captured=True
                    if len(wins)==1 and int(elapsed//20)>capture_step:
                        capture_step=int(elapsed//20)
                        try:ImageGrab.grab(window=wins[0]['hwnd']).save(d/f'state_{capture_step:02d}.png')
                        except Exception as e:meta['capture_error']=repr(e)
                    if 'TARGET' in meta['milestones'] and 'PSS_REACHED' in meta['milestones']:
                        if args.try_movie_skip and not skip_sent and elapsed-meta['milestones']['TARGET']>=5:
                            tail=text[text.rfind('[GP:H]'):]
                            if '[MPEG:diag] frame decoded' in tail or 'getPicture ENTRY #2' in tail:
                                raise RuntimeError('Playback state changed; movie skip cancelled')
                            padfile.write_text('FFF7');time.sleep(.25);padfile.write_text('FFFF')
                            meta['inputs'].append(dict(at=elapsed,key='ENTER',duration=.25,method='runtime',observed='PSS_REACHED + GP:H stall; single movie skip attempt'))
                            skip_sent=True
                        if elapsed-meta['milestones']['TARGET']>=args.post_target:
                            meta['result']='SUCCESS';break
                    if proc.poll() is not None:
                        meta['result']='CRASH' if proc.returncode else 'EXIT_BEFORE_TARGET';break
                    if elapsed>args.boot_timeout and not wins:
                        meta['result']='BOOT_ONLY';meta['timeout_phase']='BOOT';break
                    if args.confirm and elapsed>args.menu_timeout and len(meta['inputs'])<len(keys):
                        meta['result']='INPUT_FAILED';meta['timeout_phase']='MENU';break
                    if elapsed>args.pss_timeout and 'PSS_REACHED' not in meta['milestones']:
                        meta['result']='PSS_NOT_REACHED';meta['timeout_phase']='PSS';break
                    if 'PSS_REACHED' in meta['milestones'] and 'TARGET' not in meta['milestones'] and elapsed-meta['milestones']['PSS_REACHED']>args.target_timeout:
                        meta['result']='TARGET_NOT_REACHED';meta['timeout_phase']='TARGET';break
                    # Stop input immediately once any movie starts. No title/save navigation.
                    ready=('MC_CHECK' in meta['milestones'] and elapsed>=10) if not meta['inputs'] else (elapsed-last>=4)
                    if args.confirm and ready and len(meta['inputs'])<len(keys) and '[CDMODULE/MOVIE:start]' not in text and len(wins)==1:
                        hwnd=wins[0]['hwnd'];owner=w.DWORD();u.GetWindowThreadProcessId(hwnd,c.byref(owner))
                        if owner.value!=proc.pid:raise RuntimeError('Window ownership changed')
                        ImageGrab.grab(window=hwnd).save(d/f'input_{len(meta["inputs"])+1:02d}.png')
                        key=keys[len(meta['inputs'])];vk,scan=keymap[key]
                        if args.method=='runtime':
                            mask={'ENTER':0xFFF7,'X':0xBFFF,'C':0xDFFF}[key]
                            padfile.write_text(f'{mask:04X}');down=True
                        elif args.method=='sendinput':
                            u.SetForegroundWindow(hwnd);time.sleep(.1)
                            down=send_key(hwnd,vk,scan)
                        else:down=bool(u.PostMessageW(hwnd,0x100,vk,1|(scan<<16)))
                        time.sleep(.25)
                        if args.method=='runtime':padfile.write_text('FFFF');up=True
                        else:up=send_key(hwnd,vk,scan,True) if args.method=='sendinput' else bool(u.PostMessageW(hwnd,0x101,vk,1|(scan<<16)|(3<<30)))
                        meta['inputs'].append(dict(at=elapsed,key=key,duration=.25,method=args.method,hwnd=hwnd,down=down,up=up,observed='MC_CHECK; bounded boot sequence; movie marker absent'))
                        time.sleep(1)
                        ImageGrab.grab(window=hwnd).save(d/f'after_{len(meta["inputs"]):02d}.png')
                        last=elapsed
                    (d/'result.json').write_text(json.dumps(meta,indent=2))
                    time.sleep(.25)
                else:
                    meta['result']='TARGET_NOT_REACHED' if 'PSS_REACHED' in meta['milestones'] else ('PSS_NOT_REACHED' if len(meta['inputs'])==len(keys) else ('INPUT_FAILED' if meta['inputs'] else 'BOOT_ONLY'))
        except Exception as e:
            meta['exception']=repr(e);meta['result']='HARNESS_ERROR'
        finally:
            padfile.write_text('FFFF')
            if proc is not None:
                meta['exit_before_stop']=proc.poll()
                if proc.poll() is None:
                    proc.terminate();proc.wait(timeout=15);meta['stopped_own_pid']=proc.pid
                meta['exit_code']=proc.returncode
            meta['elapsed']=time.monotonic()-t
            (d/'result.json').write_text(json.dumps(meta,indent=2))
            print(json.dumps(meta),flush=True)
finally:
    lock.unlink()
