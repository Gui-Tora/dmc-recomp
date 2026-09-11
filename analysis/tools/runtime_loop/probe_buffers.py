"""Offline evidence only. Does not feed or modify the running game."""
import hashlib,json,subprocess,sys
from pathlib import Path
d=Path(sys.argv[1]);pss=(d/'pss_preinit.bin').read_bytes();guest=(d/'video_es_preinit.bin').read_bytes()
# This run consumed 262144 bytes before init (R0 demuxed). Only parse full
# MPEG-2 video PES packets within that prefix; validate flags/header lengths.
limit=262144;pos=0;es=bytearray();packets=[]
while True:
    start=pss.find(b'\x00\x00\x01\xe0',pos,limit)
    if start<0:break
    size=int.from_bytes(pss[start+4:start+6],'big');end=start+6+size
    assert end<=limit and size>3 and pss[start+6]&0xc0==0x80
    payload=start+9+pss[start+8];assert payload<=end
    packets.append({'pss_offset':start,'es_offset':len(es),'size':end-payload})
    es.extend(pss[payload:end]);pos=end
(d/'demuxed_video_expected.es').write_bytes(es)
first=next((i for i,(x,y) in enumerate(zip(es,guest)) if x!=y),None)
result={'packet_count':len(packets),'es_bytes':len(es),'expected_sha256':hashlib.sha256(es).hexdigest(),'first_difference_against_guest_base':first,'expected_first64':es[:64].hex(),'guest_first64':guest[:64].hex(),'packets':packets,'ffprobe':{}}
for file,fmt in [('video_es_preinit.bin','mpegvideo'),('pss_preinit.bin','mpeg'),('demuxed_video_expected.es','mpegvideo')]:
    cmd=['ffprobe','-v','error','-f',fmt,'-count_frames','-show_entries','stream=codec_name,width,height,nb_read_frames','-of','json',str(d/file)]
    p=subprocess.run(cmd,capture_output=True,text=True)
    (d/(file+'.ffprobe.json')).write_text(p.stdout)
    (d/(file+'.ffprobe.stderr.txt')).write_text(p.stderr)
    result['ffprobe'][file]={'command':cmd,'exit_code':p.returncode,'stdout':p.stdout,'stderr':p.stderr}
(d/'buffer_probe.json').write_text(json.dumps(result,indent=2))
print(json.dumps({k:v for k,v in result.items() if k!='packets'},indent=2))
