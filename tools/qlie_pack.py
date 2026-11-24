#!/usr/bin/env python3
"""QLIE 3.0 reader for WHJ2; algorithms adapted from GARbro (morkt, MIT).
Original archives are always opened read-only. See THIRD_PARTY.md.
"""
import struct,json,argparse
from pathlib import Path
MASK64=(1<<64)-1

def padd(a,b,n):
 mask=(1<<n)-1
 return sum((((a>>i)&mask)+((b>>i)&mask)&mask)<<i for i in range(0,64,n))

def hash_v3(b):
 h=k=0
 for (d,) in struct.iter_unpack('<Q',b[:len(b)//8*8]):
  h=padd(h,0x0307030703070307,16);k=padd(k,d^h,16)
 return (k^(k>>32))&0xffffffff

def decrypt(b,key):
 out=bytearray(b);h=0xA73C5F9DA73C5F9D;x=((len(b)+key)&0xffffffff)^0xFEC9753E;x|=x<<32
 for i in range(0,len(b)//8*8,8):
  h=padd(h,0xCE24F523CE24F523,32)^x;x=struct.unpack_from('<Q',b,i)[0]^h;struct.pack_into('<Q',out,i,x)
 return bytes(out)

def decompress(b):
 if b[:4]!=b'1PC\xff':raise ValueError('Not QLIE compressed data')
 size=struct.unpack_from('<I',b,8)[0];out=bytearray();pos=12
 while pos<len(b):
  left=list(range(256));right=[0]*256;i=0
  while i<256:
   count=b[pos];pos+=1
   if count>127:i+=count-127;count=0
   if i>255:break
   for _ in range(count+1):
    left[i]=b[pos];pos+=1
    if left[i]!=i:right[i]=b[pos];pos+=1
    i+=1
  fmt='<H' if b[4]&1 else '<I';count=struct.unpack_from(fmt,b,pos)[0];pos+=struct.calcsize(fmt)
  for v in b[pos:pos+count]:
   stack=[v]
   while stack:
    v=stack.pop()
    if left[v]==v:out.append(v)
    else:stack.extend([right[v],left[v]])
    if len(out)>size:raise ValueError('Output overflow')
  pos+=count
 if len(out)!=size:raise ValueError(f'Output size {len(out)} != {size}')
 return bytes(out)

class MT:
 def __init__(self,seed,*keys):
  self.s=[seed&0xffffffff]
  for i in range(1,64):self.s.append((0x6611bc19*(self.s[-1]^(self.s[-1]>>30))+i)&0xffffffff)
  for key in keys:
   for i in range(min(len(key)//4,64)):self.s[i]^=struct.unpack_from('<I',key,4*i)[0]
  self.i=64
 def rand(self):
  s=self.s
  if self.i==64:
   for j in range(63):s[j]=s[(j+39)%64]^((s[j]&0x80000000)|((s[j+1]&0x7fffffff)>>1))^(0x9908b0df if s[j+1]&1 else 0)
   s[63]=s[38]^((s[63]&0x80000000)|((s[0]&0x7fffffff)>>1))^(0x9908b0df if s[62]&1 else 0);self.i=0
  y=s[self.i];self.i+=1;y^=y>>11;y^=(y<<7)&0x9c4f88e3;y^=(y<<15)&0xe7f70000;y^=y>>18
  return y&0xffffffff
 def qword(self):return self.rand()|(self.rand()<<32)

def decrypt_external(b,key,name,k1,k2):
 h=0x85f532;seed=0x33f641
 for i,v in enumerate(name):h=(h+(i&255)*v)&0xffffffff;seed^=h
 seed=(seed+(key^(7*(len(b)&0xffffff)+len(b)+h+(h^len(b)^0x8f32dc))))&0xffffffff
 seed=9*(seed&0xffffff)
 if k2:seed^=0x453a
 mt=MT(seed,k1,k2);table=[mt.qword() for _ in range(16)]
 for _ in range(9):mt.rand()
 h=mt.qword();t=mt.rand()&15;out=bytearray(b)
 for i in range(0,len(b)//8*8,8):
  h=padd(h^table[t],table[t],32);d=struct.unpack_from('<Q',b,i)[0]^h;struct.pack_into('<Q',out,i,d)
  h=padd(h,d,8)^d;h=padd((h<<1)&0xfffffffefffffffe,d,16);t=(t+1)&15
 return bytes(out)

class Pack:
 def __init__(self,path,exe=None):
  from qlie_key import get_keys
  self.k1,self.k2,_=get_keys(exe or Path(path).parents[1]/'美少女万華鏡２.exe')
  self.path=Path(path);self.fp=self.path.open('rb');fp=self.fp;fp.seek(-28,2);tr=fp.read()
  if not tr.startswith(b'FilePackVer3.0'):raise ValueError('Unsupported archive version')
  count,offset=struct.unpack_from('<IQ',tr,16);fp.seek(-0x41c,2);self.key=hash_v3(fp.read(256))&0xfffffff;fp.seek(offset);self.entries=[]
  for _ in range(count):
   n,=struct.unpack('<H',fp.read(2));raw=fp.read(n);nk=n+(self.key^0x3e)
   name=bytes(v^((((k+1)^nk)+k+1)&255) for k,v in enumerate(raw)).decode('cp932')
   off,size,unpacked,packed,encrypted,crc=struct.unpack('<QIIIII',fp.read(28))
   if off+size>offset:raise ValueError('Entry bounds')
   self.entries.append(dict(name=name,offset=off,size=size,unpacked=unpacked,packed=packed,encrypted=encrypted,hash=crc))
  first=next((e for e in self.entries if 'pack_keyfile' in e['name']),None)
  self.embedded=None
  if first:
   self.embedded=self.read(first);self.k1=self.embedded
 def read(self,e):
  self.fp.seek(e['offset']);b=self.fp.read(e['size'])
  if hash_v3(b)!=e['hash']:raise ValueError('Archive checksum mismatch')
  if 'pack_keyfile' in e['name'] and self.embedded is not None:return self.embedded
  if e['encrypted']:b=decrypt_external(b,self.key,e['name'].encode('cp932'),self.k1,self.k2)
  if e['packed']:b=decompress(b)
  if len(b)!=e['unpacked']:raise ValueError('Unpacked size')
  return b
 def close(self):self.fp.close()

if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('game',type=Path);ap.add_argument('--out',type=Path,default=Path('local/extracted'));ap.add_argument('--all',action='store_true');a=ap.parse_args();index=[]
 for path in sorted((a.game/'GameData').glob('*.pack')):
  p=Pack(path);print(path.name,len(p.entries),hex(p.key))
  for e in p.entries:
   index.append(dict(pack=path.name,**e))
   if a.all or e['name'].lower().endswith(('.s','.txt','.csv','.key')):
    target=a.out/path.stem/Path(e['name'].replace('\\','/'));target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(p.read(e))
  p.close()
 a.out.mkdir(parents=True,exist_ok=True);(a.out/'index.json').write_text(json.dumps(index,ensure_ascii=False,indent=2));print('entries',len(index))
