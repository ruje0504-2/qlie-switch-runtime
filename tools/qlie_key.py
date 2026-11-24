"""Read PE resources and the QLIE icon key without executing the EXE."""
from pathlib import Path
import struct

def pe_resources(path):
 b=Path(path).read_bytes();pe=struct.unpack_from('<I',b,0x3c)[0]
 if b[:2]!=b'MZ' or b[pe:pe+4]!=b'PE\0\0':raise ValueError('Not PE')
 n=struct.unpack_from('<H',b,pe+6)[0];opt=pe+24;optsize=struct.unpack_from('<H',b,pe+20)[0]
 sec=opt+optsize;sections=[struct.unpack_from('<8sIIII',b,sec+40*i) for i in range(n)]
 def off(rva):
  for name,vs,va,sz,ptr in sections:
   if va<=rva<va+sz:return ptr+rva-va
  raise ValueError('Unmapped RVA')
 rrva,rsize=struct.unpack_from('<II',b,opt+96+16);base=off(rrva)
 def walk(pos,path):
  named,ids=struct.unpack_from('<HH',b,base+pos+12)
  for i in range(named+ids):
   name,target=struct.unpack_from('<II',b,base+pos+16+8*i)
   if name>>31:
    at=base+(name&0x7fffffff);ln=struct.unpack_from('<H',b,at)[0];key=b[at+2:at+2+2*ln].decode('utf-16le')
   else:key=str(name)
   if target>>31:yield from walk(target&0x7fffffff,path+[key])
   else:
    rva,sz=struct.unpack_from('<II',b,base+target);at=off(rva)
    yield path+[key],at,b[at:at+sz]
 return list(walk(0,[]))

def get_keys(path):
 resources=pe_resources(path);reskey=None;icon=None;anchors={}
 keyfile=Path(path).parent/'DLL/key.fkey'
 if keyfile.is_file():reskey=keyfile.read_bytes();anchors['external_fkey']={'path':str(keyfile),'size':len(reskey)}
 for names,offset,data in resources:
  if names[:2]==['10','RESKEY']:reskey=data;anchors['RESKEY']={'offset':offset,'size':len(data)}
  if names[:2]==['10','TFORM1']:
   start=data.find(b'IconKeyImage');pic=data.find(b'Picture.Data',start);pos=pic+len(b'Picture.Data')
   if start<0 or pic<0 or data[pos]!=10:raise ValueError('QLIE icon property missing')
   size=struct.unpack_from('<I',data,pos+1)[0];blob=data[pos+5:pos+5+size]
   if len(blob)<262 or blob[:6]!=b'\x05TIcon':raise ValueError('QLIE icon data missing')
   icon=blob[6:262];anchors['IconKeyImage']={'offset':offset+pos+5+6,'size':256}
 if icon is None:raise ValueError('Game icon key missing')
 return reskey or b'',icon,anchors
