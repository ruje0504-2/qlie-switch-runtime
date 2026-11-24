// SPDX-License-Identifier: GPL-2.0-or-later
// QLIE algorithms adapted from GARbro (morkt, MIT); see THIRD_PARTY.md.
#include "qlie.hpp"
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include "cp932.inc"
namespace qlie {
static uint64_t rd(const Bytes& b,size_t p,unsigned n){if(p>b.size()||n>b.size()-p)throw std::runtime_error("Truncated QLIE data");uint64_t v=0;for(unsigned i=0;i<n;i++)v|=uint64_t(b[p+i])<<(8*i);return v;}
static void wr(Bytes&b,size_t p,uint64_t v){for(int i=0;i<8;i++)b[p+i]=uint8_t(v>>(8*i));}
static uint64_t padd(uint64_t a,uint64_t b,int n){uint64_t v=0,m=(uint64_t(1)<<n)-1;for(int i=0;i<64;i+=n)v|=(((a>>i)+(b>>i))&m)<<i;return v;}
uint32_t hash(const Bytes& b){uint64_t h=0,k=0;for(size_t p=0;p+8<=b.size();p+=8){h=padd(h,0x0307030703070307ULL,16);k=padd(k,rd(b,p,8)^h,16);}return uint32_t(k^(k>>32));}
struct MT {
 uint32_t s[64];unsigned i=64;
 explicit MT(uint32_t seed,const Bytes&k1,const Bytes&k2){s[0]=seed;for(unsigned j=1;j<64;j++)s[j]=0x6611bc19u*(s[j-1]^(s[j-1]>>30))+j;for(auto k:{&k1,&k2})for(size_t j=0;j<std::min<size_t>(k->size()/4,64);j++)s[j]^=uint32_t(rd(*k,4*j,4));}
 uint32_t get(){if(i==64){for(int j=0;j<63;j++)s[j]=s[(j+39)%64]^((s[j]&0x80000000)|((s[j+1]&0x7fffffff)>>1))^((s[j+1]&1)?0x9908b0df:0);s[63]=s[38]^((s[63]&0x80000000)|((s[0]&0x7fffffff)>>1))^((s[62]&1)?0x9908b0df:0);i=0;}uint32_t y=s[i++];y^=y>>11;y^=(y<<7)&0x9c4f88e3;y^=(y<<15)&0xe7f70000;y^=y>>18;return y;}
 uint64_t get64(){uint64_t a=get();return a|(uint64_t(get())<<32);}
};
static Bytes decrypt(Bytes b,uint32_t arc,const std::string&name,const Bytes&k1,const Bytes&k2){uint32_t h=0x85f532,seed=0x33f641,len=uint32_t(b.size());for(size_t j=0;j<name.size();j++){h+=uint32_t(j&255)*uint8_t(name[j]);seed^=h;}seed+=arc^(7*(len&0xffffff)+len+h+(h^len^0x8f32dc));seed=9*(seed&0xffffff);if(!k2.empty())seed^=0x453a;MT mt(seed,k1,k2);uint64_t table[16];for(auto&x:table)x=mt.get64();for(int j=0;j<9;j++)mt.get();uint64_t hs=mt.get64();unsigned t=mt.get()&15;for(size_t p=0;p+8<=b.size();p+=8){hs=padd(hs^table[t],table[t],32);uint64_t d=rd(b,p,8)^hs;wr(b,p,d);hs=padd(hs,d,8)^d;hs=padd((hs<<1)&0xfffffffefffffffeULL,d,16);t=(t+1)&15;}return b;}
Bytes decompress(const Bytes&b){if(b.size()<12||std::memcmp(b.data(),"1PC\xff",4))throw std::runtime_error("Not QLIE compressed data");size_t size=rd(b,8,4),p=12;Bytes out;out.reserve(size);while(p<b.size()){uint8_t l[256],r[256]={};for(int i=0;i<256;i++)l[i]=uint8_t(i);for(int i=0;i<256;){unsigned c=rd(b,p++,1);if(c>127){i+=int(c-127);c=0;}if(i>255)break;for(unsigned j=0;j<=c;j++){if(i>=256)throw std::runtime_error("Invalid BPE table");l[i]=uint8_t(rd(b,p++,1));if(l[i]!=i)r[i]=uint8_t(rd(b,p++,1));i++;}}unsigned cb=(b[4]&1)?2:4;size_t count=rd(b,p,cb);p+=cb;if(count>b.size()-p)throw std::runtime_error("Truncated BPE block");for(size_t j=0;j<count;j++){std::vector<uint8_t> stack{b[p++]};unsigned steps=0;while(!stack.empty()){unsigned v=stack.back();stack.pop_back();if(l[v]==v){if(out.size()==size)throw std::runtime_error("BPE overflow");out.push_back(uint8_t(v));}else{stack.push_back(r[v]);stack.push_back(l[v]);}if(++steps>size*2+256)throw std::runtime_error("Cyclic BPE");}}}if(out.size()!=size)throw std::runtime_error("BPE size mismatch");return out;}
std::string cp932(const Bytes&b){std::string out;for(size_t p=0;p<b.size();p++){unsigned a=b[p],cp=cp932_single[a];if(!cp&&a){if(p+1==b.size())throw std::runtime_error("Truncated CP932");unsigned c=b[++p];if(a<0x81||a>0xfe||c<0x40||c>0xfe||c==0x7f)throw std::runtime_error("Invalid CP932");cp=cp932_double[(a-0x81)*190+c-0x40-(c>0x7f)];if(!cp)throw std::runtime_error("Unknown CP932 pair");}if(cp<128)out+=char(cp);else if(cp<2048){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}else{out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}}return out;}
std::string canonical(std::string s){for(auto&c:s){if(c==92)c=47;else if(c>=65&&c<=90)c+=32;}while(s.compare(0,2,"./")==0)s.erase(0,2);return s;}
Bytes file(const std::string&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot open "+p);f.seekg(0,std::ios::end);auto n=f.tellg();if(n<0)throw std::runtime_error("Cannot size "+p);f.seekg(0);Bytes b(static_cast<size_t>(n));if(!b.empty()&&!f.read(reinterpret_cast<char*>(b.data()),b.size()))throw std::runtime_error("Cannot read "+p);return b;}
void write_file(const std::string&p,const Bytes&b){std::ofstream f(p,std::ios::binary|std::ios::trunc);if(!f||!f.write(reinterpret_cast<const char*>(b.data()),b.size()))throw std::runtime_error("Cannot write "+p);}
Pack::Pack(const std::string&path,const Bytes&a,const Bytes&b):fp(path,std::ios::binary),k1(a),k2(b){if(!fp)throw std::runtime_error("Cannot open "+path);fp.seekg(0,std::ios::end);uint64_t n=uint64_t(fp.tellg());if(n<0x41c)throw std::runtime_error("Archive too short");Bytes tail(28),kd(256);fp.seekg(n-28);fp.read(reinterpret_cast<char*>(tail.data()),28);if(std::memcmp(tail.data(),"FilePackVer3.0",13))throw std::runtime_error("Unsupported QLIE version");unsigned count=unsigned(rd(tail,16,4));uint64_t offset=rd(tail,20,8);if(count>100000||offset>n-28)throw std::runtime_error("Invalid archive index");fp.seekg(n-0x41c);fp.read(reinterpret_cast<char*>(kd.data()),256);key=hash(kd)&0xfffffff;fp.seekg(offset);Bytes toc(n-28-offset);fp.read(reinterpret_cast<char*>(toc.data()),toc.size());size_t p=0;for(unsigned i=0;i<count;i++){size_t len=rd(toc,p,2);p+=2;if(len>256||p+len+28>toc.size())throw std::runtime_error("Invalid entry name");Bytes name(toc.begin()+p,toc.begin()+p+len);unsigned nk=unsigned(len)+(key^0x3e);for(unsigned j=0;j<len;j++)name[j]^=uint8_t(((j+1)^nk)+j+1);p+=len;Entry e{cp932(name),std::string(name.begin(),name.end()),rd(toc,p,8),uint32_t(rd(toc,p+8,4)),uint32_t(rd(toc,p+12,4)),uint32_t(rd(toc,p+16,4)),uint32_t(rd(toc,p+20,4)),uint32_t(rd(toc,p+24,4))};p+=28;if(e.offset+e.size>offset)throw std::runtime_error("Invalid entry range");entries.push_back(e);}for(const auto&e:entries)if(e.name.find("pack_keyfile")!=std::string::npos){key_entry=read(e);k1=key_entry;key_ready=true;break;}}
Bytes Pack::read(const Entry&e){if(key_ready&&e.name.find("pack_keyfile")!=std::string::npos)return key_entry;Bytes b(e.size);fp.clear();fp.seekg(e.offset);if(!fp.read(reinterpret_cast<char*>(b.data()),b.size()))throw std::runtime_error("Entry read failed: "+e.name);if(hash(b)!=e.hash)throw std::runtime_error("Checksum mismatch: "+e.name);if(e.encrypted){if(e.encrypted!=4)throw std::runtime_error("Unsupported entry encryption");b=decrypt(std::move(b),key,e.rawname,k1,k2);}if(e.packed)b=decompress(b);if(b.size()!=e.unpacked)throw std::runtime_error("Entry size mismatch: "+e.name);return b;}
std::pair<Bytes,Bytes> game_keys(const std::string&root){Bytes k1=file(root+"/DLL/key.fkey"),exe=file(root+"/美少女万華鏡２.exe");std::string pat="\x05TIcon";size_t at=std::string(reinterpret_cast<char*>(exe.data()),exe.size()).rfind(pat);if(at==std::string::npos||at+262>exe.size())throw std::runtime_error("Missing QLIE icon key");Bytes k2(exe.begin()+at+6,exe.begin()+at+262);return {k1,k2};}
namespace {
std::optional<std::string> looseAudioPath(const std::string&root,const std::string&name){
 auto extension=std::filesystem::path(name).extension().string();
 // Native FileIO checks loose files before package slots. Keep this addition
 // to the audio paths required by the player; script-file rules are separate.
 if(extension!=".ogg"&&extension!=".wav"&&extension!=".ogv"&&name.rfind("voice/",0)!=0)return {};
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
Assets::Assets(const std::string&directory):root(directory){
 auto keys=game_keys(root);
 for(int slot=0;slot<16;slot++){
  auto path=root+"/GameData/data"+std::to_string(slot)+".pack";
  if(!std::filesystem::is_regular_file(path))continue;
  auto pack=std::make_unique<Pack>(path,keys.first,keys.second);
  for(const auto&e:pack->entries)if(e.name.find("pack_keyfile")==std::string::npos){auto key=canonical(e.name);index[key]={pack.get(),e};sources[key]={key,path,e.offset,e.size,false};}
  packs.push_back(std::move(pack));
 }
 if(packs.empty())throw std::runtime_error("No numbered QLIE resource packs in "+root);
}
std::optional<ResourceLocation> Assets::locate(const std::string&name)const{
 auto key=canonical(name);
 if(auto path=looseAudioPath(root,key))return ResourceLocation{key,*path,0,std::filesystem::file_size(*path),true};
 auto it=sources.find(key);
 if(it!=sources.end())return it->second;
 return {};
}
Bytes Assets::read(const std::string&name){
 auto location=locate(name);
 if(!location)throw std::runtime_error("Missing resource: "+name);
 if(location->loose)return file(location->path);
 auto it=index.find(canonical(name));
 try{return it->second.first->read(it->second.second);}catch(const std::exception&e){throw std::runtime_error(name+": "+e.what());}
}
bool Assets::exists(const std::string&name)const{return locate(name).has_value();}
std::vector<std::string> Assets::names()const{std::vector<std::string>n;for(const auto&e:index)n.push_back(e.first);return n;}
}
