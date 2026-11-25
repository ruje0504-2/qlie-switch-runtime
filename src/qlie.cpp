// SPDX-License-Identifier: GPL-2.0-or-later
// QLIE algorithms adapted from GARbro (morkt, MIT); see THIRD_PARTY.md.
#include "qlie.hpp"
#include "switch_storage.hpp"
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <string_view>
#include "cp932.inc"
namespace qlie {
static uint64_t rd(const Bytes& b,size_t p,unsigned n){if(p>b.size()||n>b.size()-p)throw std::runtime_error("Truncated QLIE data");uint64_t v=0;for(unsigned i=0;i<n;i++)v|=uint64_t(b[p+i])<<(8*i);return v;}
static void wr(Bytes&b,size_t p,uint64_t v){for(int i=0;i<8;i++)b[p+i]=uint8_t(v>>(8*i));}
static uint64_t padd(uint64_t a,uint64_t b,int n){uint64_t v=0,m=(uint64_t(1)<<n)-1;for(int i=0;i<64;i+=n)v|=(((a>>i)+(b>>i))&m)<<i;return v;}
uint32_t hash(const Bytes& b){uint64_t h=0,k=0;for(size_t p=0;p+8<=b.size();p+=8){h=padd(h,0x0307030703070307ULL,16);k=padd(k,rd(b,p,8)^h,16);}return uint32_t(k^(k>>32));}
static uint32_t hash31(const Bytes& bytes){
 uint64_t counter=0,state=0;
 for(size_t pos=0;pos+8<=bytes.size();pos+=8){
  counter=padd(counter,0xa35793a7a35793a7ULL,16);
  state=padd(state,rd(bytes,pos,8)^counter,16);
  state=((state<<3)&0xfffffff8fffffff8ULL)|((state>>29)&0x0000000700000007ULL);
 }
 const auto signedWord=[&](unsigned shift){
  int word=int((state>>shift)&0xffff);
  return word>=0x8000?word-0x10000:word;
 };
 return uint32_t(int64_t(signedWord(0))*signedWord(32)+int64_t(signedWord(16))*signedWord(48));
}

static void putUtf8(std::string& out,uint32_t cp){
 if(cp<0x80){out+=char(cp);return;}
 if(cp<0x800){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));return;}
 if(cp<0x10000)out+=char(0xe0|(cp>>12));
 else{out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));}
 out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));
}

std::string utf16le(const Bytes& bytes,size_t offset){
 if(offset>bytes.size()||(bytes.size()-offset)%2)throw std::runtime_error("Truncated UTF-16LE");
 std::string text;
 for(size_t pos=offset;pos<bytes.size();pos+=2){
  uint32_t cp=uint32_t(rd(bytes,pos,2));
  if(cp>=0xd800&&cp<=0xdbff){
   pos+=2;
   const auto low=rd(bytes,pos,2);
   if(low<0xdc00||low>0xdfff)throw std::runtime_error("Invalid UTF-16LE surrogate pair");
   cp=0x10000+((cp-0xd800)<<10)+uint32_t(low-0xdc00);
  }else if(cp>=0xdc00&&cp<=0xdfff)throw std::runtime_error("Invalid UTF-16LE surrogate");
  putUtf8(text,cp);
 }
 return text;
}

static void decrypt31(Bytes& bytes,uint32_t arc,const std::string& name,unsigned method,const Bytes& keyFile){
 if(method!=1&&method!=2)throw std::runtime_error("Unsupported QLIE 3.1 entry encryption");
 const bool secondary=method==2;
 uint32_t hash=secondary?0x86f7e2u:0x85f532u;
 uint32_t seed=secondary?0x4437f1u:0x33f641u;
 for(size_t pos=0;pos<name.size();pos+=2){
  const uint32_t word=uint8_t(name[pos])|(uint32_t(uint8_t(name[pos+1]))<<8);
  hash+=word<<((pos/2)&7);
  seed^=hash;
 }
 const uint32_t length=uint32_t(bytes.size()),factor=secondary?13u:7u;
 const uint32_t lengthConstant=secondary?0x56e213u:0x8f32dcu;
 seed+=arc^(factor*(length&0xffffff)+length+hash+(hash^length^lengthConstant));
 seed=(secondary?13u:9u)*(seed&0xffffff);
 const uint32_t multiplier=secondary?0x8a77f473u:0x8df21431u;
 uint32_t table[32];
 for(auto& word:table){
  const uint64_t product=uint64_t(multiplier)*(seed^multiplier);
  word=seed=uint32_t((product>>32)+product);
 }
 Bytes overlay;
 if(secondary){
  overlay.resize(1024);
  for(int i=0;i<256;++i){
   const auto word=uint32_t((i+7)*(i+3)*(i%3?-1:1));
   for(unsigned j=0;j<4;++j)overlay[4*i+j]=uint8_t(word>>(8*j));
  }
  if(keyFile.size()>=128){
   size_t offset=keyFile[49]%73+128;
   const auto step=keyFile[79]%7+7;
   for(auto& byte:overlay){offset=(offset+step)%keyFile.size();byte^=keyFile[offset];}
  }
 }
 unsigned index=secondary?8*(table[8]&13u):table[13]&15u;
 uint64_t state=table[6]|(uint64_t(table[7])<<32);
 for(size_t pos=0;pos+8<=bytes.size();pos+=8){
  const unsigned slot=2*(index&15);
  uint64_t word=table[slot]|(uint64_t(table[slot+1])<<32);
  if(secondary)word^=rd(overlay,8*index,8);
  state=padd(state^word,word,32);
  const uint64_t plain=rd(bytes,pos,8)^state;
  wr(bytes,pos,plain);
  state=padd(state,plain,8)^plain;
  state=padd((state<<1)&0xfffffffefffffffeULL,plain,16);
  index=(index+1)&(secondary?127:15);
 }
}

struct MT {
 uint32_t s[64];unsigned i=64;
 explicit MT(uint32_t seed,const Bytes&k1,const Bytes&k2){s[0]=seed;for(unsigned j=1;j<64;j++)s[j]=0x6611bc19u*(s[j-1]^(s[j-1]>>30))+j;for(auto k:{&k1,&k2})for(size_t j=0;j<std::min<size_t>(k->size()/4,64);j++)s[j]^=uint32_t(rd(*k,4*j,4));}
 uint32_t get(){if(i==64){for(int j=0;j<63;j++)s[j]=s[(j+39)%64]^((s[j]&0x80000000)|((s[j+1]&0x7fffffff)>>1))^((s[j+1]&1)?0x9908b0df:0);s[63]=s[38]^((s[63]&0x80000000)|((s[0]&0x7fffffff)>>1))^((s[62]&1)?0x9908b0df:0);i=0;}uint32_t y=s[i++];y^=y>>11;y^=(y<<7)&0x9c4f88e3;y^=(y<<15)&0xe7f70000;y^=y>>18;return y;}
 uint64_t get64(){uint64_t a=get();return a|(uint64_t(get())<<32);}
};
static Bytes decrypt(Bytes b,uint32_t arc,const std::string&name,const Bytes&k1,const Bytes&k2){uint32_t h=0x85f532,seed=0x33f641,len=uint32_t(b.size());for(size_t j=0;j<name.size();j++){h+=uint32_t(j&255)*uint8_t(name[j]);seed^=h;}seed+=arc^(7*(len&0xffffff)+len+h+(h^len^0x8f32dc));seed=9*(seed&0xffffff);if(!k2.empty())seed^=0x453a;MT mt(seed,k1,k2);uint64_t table[16];for(auto&x:table)x=mt.get64();for(int j=0;j<9;j++)mt.get();uint64_t hs=mt.get64();unsigned t=mt.get()&15;for(size_t p=0;p+8<=b.size();p+=8){hs=padd(hs^table[t],table[t],32);uint64_t d=rd(b,p,8)^hs;wr(b,p,d);hs=padd(hs,d,8)^d;hs=padd((hs<<1)&0xfffffffefffffffeULL,d,16);t=(t+1)&15;}return b;}
Bytes decompress(const Bytes&bytes){
 if(bytes.size()<12||std::memcmp(bytes.data(),"1PC\xff",4))
  throw std::runtime_error("Not QLIE compressed data");
 const size_t expected=rd(bytes,8,4);
 Bytes output;
 output.reserve(expected);
 std::vector<uint8_t> stack;
 stack.reserve(32);
 const unsigned countBytes=(bytes[4]&1)?2:4;
 size_t pos=12;
 while(pos<bytes.size()){
  uint8_t left[256],right[256]={};
  for(unsigned i=0;i<256;++i)left[i]=uint8_t(i);
  for(unsigned i=0;i<256;){
   unsigned count=rd(bytes,pos++,1);
   if(count>127){i+=count-127;count=0;}
   if(i>255)break;
   for(unsigned j=0;j<=count;++j){
    if(i>=256)throw std::runtime_error("Invalid BPE table");
    left[i]=uint8_t(rd(bytes,pos++,1));
    if(left[i]!=i)right[i]=uint8_t(rd(bytes,pos++,1));
    ++i;
   }
  }
  const size_t count=rd(bytes,pos,countBytes);
  pos+=countBytes;
  if(count>bytes.size()-pos)throw std::runtime_error("Truncated BPE block");
  for(size_t i=0;i<count;++i){
   // The expansion empties the stack for each code. Reuse its storage for
   // the next code and block rather than allocating once per input byte.
   stack.push_back(bytes[pos++]);
   unsigned steps=0;
   while(!stack.empty()){
    const auto current=stack.back();
    stack.pop_back();
    if(left[current]==current){
     if(output.size()==expected)throw std::runtime_error("BPE overflow");
     output.push_back(current);
    }
    else {stack.push_back(right[current]);stack.push_back(left[current]);}
    if(++steps>expected*2+256)throw std::runtime_error("Cyclic BPE");
   }
  }
 }
 if(output.size()!=expected)throw std::runtime_error("BPE size mismatch");
 return output;
}
std::string cp932(const Bytes&b){std::string out;for(size_t p=0;p<b.size();p++){unsigned a=b[p],cp=cp932_single[a];if(!cp&&a){if(p+1==b.size())throw std::runtime_error("Truncated CP932");unsigned c=b[++p];if(a<0x81||a>0xfe||c<0x40||c>0xfe||c==0x7f)throw std::runtime_error("Invalid CP932");cp=cp932_double[(a-0x81)*190+c-0x40-(c>0x7f)];if(!cp)throw std::runtime_error("Unknown CP932 pair");}if(cp<128)out+=char(cp);else if(cp<2048){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}else{out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}}return out;}
std::string canonical(std::string s){for(auto&c:s){if(c==92)c=47;else if(c>=65&&c<=90)c+=32;}while(s.compare(0,2,"./")==0)s.erase(0,2);return s;}
Bytes file(const std::string&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot open "+p);f.seekg(0,std::ios::end);auto n=f.tellg();if(n<0)throw std::runtime_error("Cannot size "+p);f.seekg(0);Bytes b(static_cast<size_t>(n));if(!b.empty()&&!f.read(reinterpret_cast<char*>(b.data()),b.size()))throw std::runtime_error("Cannot read "+p);return b;}
void write_uncommitted_file(const std::string&p,const Bytes&b){std::ofstream f(p,std::ios::binary|std::ios::trunc);if(!f||!f.write(reinterpret_cast<const char*>(b.data()),b.size()))throw std::runtime_error("Cannot write "+p);f.close();if(!f)throw std::runtime_error("Cannot close "+p);}
void write_file(const std::string&p,const Bytes&b){write_uncommitted_file(p,b);whj2::commitSavePath(p);}
Pack::Pack(const std::string& path,const Bytes& a,const Bytes& b,PackVersion expected,bool useEmbeddedFileKey)
 :fp(path,std::ios::binary),k1(a),k2(b),version(expected){
 if(!fp)throw std::runtime_error("Cannot open "+path);
 fp.seekg(0,std::ios::end);
 const auto end=fp.tellg();
 if(end<0x41c)throw std::runtime_error("Archive too short");
 const uint64_t size=uint64_t(end);
 Bytes tail(28),keyData(256);
 fp.seekg(size-tail.size());
 if(!fp.read(reinterpret_cast<char*>(tail.data()),tail.size()))throw std::runtime_error("Archive footer read failed");
 constexpr std::string_view signature30="FilePackVer3.0",signature31="FilePackVer3.1";
 const std::string_view signature(reinterpret_cast<const char*>(tail.data()),signature30.size());
 if(signature!=signature30&&signature!=signature31)throw std::runtime_error("Unknown QLIE archive version: "+path);
 const bool unicode=signature==signature31;
 if(unicode!=(version==PackVersion::v31))throw std::runtime_error("Resource pack version differs from game profile: "+path);
 const auto count=rd(tail,16,4);
 const auto index=rd(tail,20,8);
 if(count>100000||index>size-tail.size())throw std::runtime_error("Invalid archive index");
 fp.seekg(size-0x41c);
 if(!fp.read(reinterpret_cast<char*>(keyData.data()),keyData.size()))throw std::runtime_error("Archive seed read failed");
 key=(unicode?hash31(keyData):hash(keyData))&0xfffffff;
 fp.seekg(index);
 Bytes toc(size-tail.size()-index);
 if(!fp.read(reinterpret_cast<char*>(toc.data()),toc.size()))throw std::runtime_error("Archive index read failed");
 size_t pos=0;
 entries.reserve(count);
 for(unsigned i=0;i<count;++i){
  const unsigned units=unsigned(rd(toc,pos,2));
  pos+=2;
  const size_t length=unicode?units*2:units;
  if(units>256||pos+length+28>toc.size())throw std::runtime_error("Invalid entry name");
  Bytes name(toc.begin()+pos,toc.begin()+pos+length);
  if(unicode){
   const auto seed=((units*units)^units^0x3e13^(key>>16)^key)&0xffff;
   unsigned state=seed;
   for(unsigned j=0;j<units;++j){
    state=(seed+j+8*state)&0xffff;
    name[j*2]^=uint8_t(state);
    name[j*2+1]^=uint8_t(state>>8);
   }
  }else{
   const auto mask=units+(key^0x3e);
   for(unsigned j=0;j<units;++j)name[j]^=uint8_t(((j+1)^mask)+j+1);
  }
  pos+=length;
  Entry entry{unicode?utf16le(name):cp932(name),std::string(name.begin(),name.end()),rd(toc,pos,8),
    uint32_t(rd(toc,pos+8,4)),uint32_t(rd(toc,pos+12,4)),uint32_t(rd(toc,pos+16,4)),
    uint32_t(rd(toc,pos+20,4)),uint32_t(rd(toc,pos+24,4))};
  pos+=28;
  if(entry.offset>index||entry.size>index-entry.offset)throw std::runtime_error("Invalid entry range");
  entries.push_back(std::move(entry));
 }
 // Version 3.0 and the embedded-key 3.1 layout bootstrap from pack_keyfile.
 if(!unicode||useEmbeddedFileKey){
  for(const auto& entry:entries){
   if(entry.name.find("pack_keyfile")==std::string::npos)continue;
   if(unicode&&(entry.encrypted!=1||entry.unpacked<128))
    throw std::runtime_error("Invalid embedded QLIE 3.1 key: "+path);
   key_entry=read(entry);k1=key_entry;key_ready=true;break;
  }
  if(unicode&&!key_ready)throw std::runtime_error("Missing embedded QLIE 3.1 key: "+path);
 }
}
uint32_t Pack::checksum(const Bytes& bytes) const{
 return version==PackVersion::v31?hash31(bytes):hash(bytes);
}
Bytes Pack::read(const Entry&e){
 if(key_ready&&e.name.find("pack_keyfile")!=std::string::npos)return key_entry;
 Bytes b(e.size);
 {
  std::lock_guard<std::mutex> lock(fileCursor);
  fp.clear();
  fp.seekg(e.offset);
  if(!fp.read(reinterpret_cast<char*>(b.data()),b.size()))throw std::runtime_error("Entry read failed: "+e.name);
 }
 if(checksum(b)!=e.hash)throw std::runtime_error("Checksum mismatch: "+e.name);
 if(e.encrypted){
  if(version==PackVersion::v31)decrypt31(b,key,e.rawname,e.encrypted,k1);
  else{
   if(e.encrypted!=4)throw std::runtime_error("Unsupported entry encryption");
   b=decrypt(std::move(b),key,e.rawname,k1,k2);
  }
 }
 if(e.packed)b=decompress(b);
 if(b.size()!=e.unpacked)throw std::runtime_error("Entry size mismatch: "+e.name);
 return b;
}
std::pair<Bytes,Bytes> game_keys(const std::string&root,const ArchiveLayout&layout){
 if(layout.version==PackVersion::v31&&layout.fileKey.empty())return {};
 Bytes k1=file(root+"/"+layout.fileKey);
 if(layout.version==PackVersion::v31){
  if(k1.size()<128)throw std::runtime_error("QLIE 3.1 key file is too short: "+layout.fileKey);
  return {std::move(k1),{}};
 }
 auto icon=root+"/"+layout.iconKey;
 if(std::filesystem::is_regular_file(icon)){auto k2=file(icon);if(k2.size()!=256)throw std::runtime_error("icon.key must contain exactly 256 bytes");return {k1,k2};}
#ifdef __SWITCH__
 throw std::runtime_error("Missing "+layout.iconKey+" in game directory");
#else
 if(layout.hostExecutable.empty())throw std::runtime_error("Missing "+layout.iconKey+" in game directory");
 auto exe=file(root+"/"+layout.hostExecutable);std::string pat="\x05TIcon";size_t at=std::string(reinterpret_cast<char*>(exe.data()),exe.size()).rfind(pat);if(at==std::string::npos||at+262>exe.size())throw std::runtime_error("Missing QLIE icon key");Bytes k2(exe.begin()+at+6,exe.begin()+at+262);return {k1,k2};
#endif
}
namespace {
std::optional<std::string> looseAudioPath(const std::string&root,const std::string&name,const std::string&voicePrefix){
 auto extension=std::filesystem::path(name).extension().string();
 // Native FileIO checks loose files before package slots. Keep this addition
 // to the audio paths required by the player; script-file rules are separate.
 if(extension!=".ogg"&&extension!=".wav"&&extension!=".ogv"&&(voicePrefix.empty()||name.rfind(voicePrefix,0)!=0))return {};
 auto relative=std::filesystem::path(name).lexically_normal();
 if(relative.is_absolute())return {};
 for(const auto&part:relative)if(part=="..")return {};
 auto path=std::filesystem::path(root);std::error_code ec;
 for(const auto&part:relative){
  auto direct=path/part;
  if(std::filesystem::exists(direct,ec)){path=direct;continue;}
  bool found=false;
  for(std::filesystem::directory_iterator it(path,ec),end;!ec&&it!=end;it.increment(ec)){
   if(canonical(it->path().filename().string())==canonical(part.string())){path=it->path();found=true;break;}
  }
  if(!found)return {};
 }
 if(!std::filesystem::is_regular_file(path,ec))return {};
 return path.string();
}
}
Assets::Assets(const std::string&directory,const ArchiveLayout&layout)
 :root(directory),looseVoicePrefix(layout.looseVoicePrefix),resourceAliases(layout.resourceAliases){
 auto keys=game_keys(root,layout);
 const auto addPack=[&](const std::string& path,const Bytes& fileKey,const Bytes& iconKey,PackVersion version){
  const bool embedded=version==PackVersion::v31&&layout.fileKey.empty();
  auto pack=std::make_unique<Pack>(path,fileKey,iconKey,version,embedded);
  for(const auto& entry:pack->entries){
   if(entry.name.find("pack_keyfile")!=std::string::npos)continue;
   auto key=canonical(entry.name);
   index[key]={pack.get(),entry};
   sources[key]={key,path,entry.offset,entry.size,false};
  }
  packs.push_back(std::move(pack));
 };
 for(unsigned slot=0;slot<layout.slotCount;slot++){
  auto path=root+"/"+layout.archivePrefix+std::to_string(slot)+".pack";
  if(std::filesystem::is_regular_file(path))addPack(path,keys.first,keys.second,layout.version);
 }
 if(packs.empty())throw std::runtime_error("No numbered QLIE resource packs in "+root);
 if(!layout.patchFile.empty()){
  const auto path=root+"/"+layout.patchFile;
  if(std::filesystem::is_regular_file(path)){
   addPack(path,{},{},PackVersion::v30);
   patchPath=path;
  }
 }
}
std::string Assets::alternateName(const std::string&name)const{
 if(auto file=resourceAliases.find(name);file!=resourceAliases.end())return file->second;
 for(auto alias=resourceAliases.rbegin();alias!=resourceAliases.rend();++alias){
  const auto&prefix=alias->first;
  if(!prefix.empty()&&prefix.back()=='/'&&name.rfind(prefix,0)==0)
   return alias->second+name.substr(prefix.size());
 }
 return name;
}
bool Assets::inPack(const std::string&name)const{
 const auto key=canonical(name);
 return index.count(key)||index.count(alternateName(key));
}
std::optional<ResourceLocation> Assets::locate(const std::string&name)const{
 const auto key=canonical(name);
 const auto alternate=alternateName(key);
 auto lookup=[&](const std::string&candidate)->std::optional<ResourceLocation>{
  auto it=sources.find(candidate);
  if(it!=sources.end()&&it->second.path==patchPath)return it->second;
  if(auto path=looseAudioPath(root,candidate,looseVoicePrefix))
   return ResourceLocation{candidate,*path,0,std::filesystem::file_size(*path),true};
  if(it!=sources.end())return it->second;
  return {};
 };
 if(auto found=lookup(key))return found;
 if(alternate!=key)return lookup(alternate);
 return {};
}
Bytes Assets::read(const std::string&name){
 auto location=locate(name);
 if(!location)throw std::runtime_error("Missing resource: "+name);
 if(location->loose)return file(location->path);
 auto it=index.find(location->name);
 try{return it->second.first->read(it->second.second);}catch(const std::exception&e){throw std::runtime_error(name+": "+e.what());}
}
std::string Assets::text(const std::string&name){
 const auto bytes=read(name);
 if(bytes.size()>=2&&bytes[0]==0xff&&bytes[1]==0xfe)return utf16le(bytes,2);
 if(bytes.size()>=3&&bytes[0]==0xef&&bytes[1]==0xbb&&bytes[2]==0xbf)
  return std::string(bytes.begin()+3,bytes.end());
 return cp932(bytes);
}
bool Assets::exists(const std::string&name)const{return locate(name).has_value();}
std::vector<std::string> Assets::names()const{std::vector<std::string>n;for(const auto&e:index)n.push_back(e.first);return n;}
}
