// SPDX-License-Identifier: GPL-2.0-or-later
#include "image.hpp"
#include "profile.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
extern "C" {
#include <libavutil/mem.h>
#include <libavutil/sha.h>
}

namespace fs=std::filesystem;
using qlie::Bytes;

namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}

bool starts(const Bytes&bytes,std::string_view magic){
 return bytes.size()>=magic.size()&&std::equal(magic.begin(),magic.end(),bytes.begin(),
     [](char expected,uint8_t actual){return static_cast<uint8_t>(expected)==actual;});
}

std::string sha256(const Bytes&bytes){
 std::unique_ptr<AVSHA,decltype(&av_free)> sha(av_sha_alloc(),av_free);
 require(sha&&av_sha_init(sha.get(),256)==0,"Cannot initialize SHA-256");
 av_sha_update(sha.get(),bytes.data(),bytes.size());
 std::array<uint8_t,32> digest;
 av_sha_final(sha.get(),digest.data());
 static constexpr char hex[]="0123456789abcdef";
 std::string text;
 for(auto byte:digest){text+=hex[byte>>4];text+=hex[byte&15];}
 return text;
}

void writeLE(Bytes&bytes,size_t offset,uint64_t value,unsigned size){
 require(offset<=bytes.size()&&size<=bytes.size()-offset,"Cannot update packed field");
 for(unsigned i=0;i<size;i++)bytes[offset+i]=uint8_t(value>>(i*8));
}

uint64_t readLE(const Bytes&bytes,size_t offset,unsigned size){
 require(offset<=bytes.size()&&size<=bytes.size()-offset,"Cannot read packed field");
 uint64_t value=0;
 for(unsigned i=0;i<size;i++)value|=uint64_t(bytes[offset+i])<<(i*8);
 return value;
}

std::optional<Bytes> replaceVideo(const Bytes&input,const fs::path&encoded,const std::string&name,bool extract){
 if(starts(input,"OggS")){
  const auto end=input.begin()+std::min<size_t>(input.size(),65536);
  const std::array<uint8_t,7> identification{0x80,'t','h','e','o','r','a'};
  if(std::search(input.begin(),end,identification.begin(),identification.end())==end)return {};
  const auto digest=sha256(input);
  if(extract){
   const auto output=encoded/(digest+".ogv");
   if(fs::exists(output))require(sha256(qlie::file(output.string()))==digest,"Extracted video differs");
   else qlie::write_file(output.string(),input);
   std::cout<<"SOURCE\t"<<digest<<"\t"<<input.size()<<"\t"<<name<<"\n";
   return {};
  }
  const auto output=encoded/(digest+".mkv");
  if(!fs::is_regular_file(output))throw std::runtime_error("Missing H.264 encoding for original video: "+name+" ("+digest+")");
  auto video=qlie::file(output.string());
  require(starts(video,"\x1a\x45\xdf\xa3"),"H.264 encoding must use Matroska");
  std::cout<<"VIDEO\t"<<digest<<"\t"<<sha256(video)<<"\t"<<input.size()<<"\t"<<video.size()<<"\t"<<name<<"\n";
  return video;
 }
 if(!starts(input,"abmp1"))return {};
 Bytes output;
 size_t cursor=0;
 for(const auto&member:whj2::abmp(input)){
  if(member.type.rfind("abimg",0)!=0)continue;
  auto replacement=replaceVideo(member.data,encoded,name+"#"+member.name,extract);
  if(!replacement)continue;
  require(replacement->size()<=UINT32_MAX&&member.dataOffset>=cursor+4,"ABMP member size or order");
  output.insert(output.end(),input.begin()+cursor,input.begin()+member.dataOffset);
  writeLE(output,output.size()-4,replacement->size(),4);
  output.insert(output.end(),replacement->begin(),replacement->end());
  cursor=member.dataOffset+member.storedSize;
 }
 if(!cursor)return {};
 output.insert(output.end(),input.begin()+cursor,input.end());
 return output;
}

void write(std::ofstream&output,const Bytes&bytes){
 if(!output.write(reinterpret_cast<const char*>(bytes.data()),bytes.size()))throw std::runtime_error("Pack write failed");
}

void copy(std::ifstream&input,std::ofstream&output,uint64_t start,uint64_t size,std::vector<char>&buffer){
 input.seekg(start);
 while(size){
  const auto count=std::min<uint64_t>(size,buffer.size());
  if(!input.read(buffer.data(),count)||!output.write(buffer.data(),count))throw std::runtime_error("Pack range copy failed");
  size-=count;
 }
}

void verifyCopy(std::ifstream&before,std::ifstream&after,const qlie::Entry&old,const qlie::Entry&fresh,
                std::vector<char>&a,std::vector<char>&b){
 require(old.size==fresh.size&&old.unpacked==fresh.unpacked&&old.packed==fresh.packed&&
         old.encrypted==fresh.encrypted&&old.hash==fresh.hash,"Unchanged pack entry metadata differs");
 before.clear();after.clear();before.seekg(old.offset);after.seekg(fresh.offset);
 size_t remaining=old.size;
 while(remaining){
  const auto count=std::min(remaining,a.size());
  if(!before.read(a.data(),count)||!after.read(b.data(),count)||!std::equal(a.begin(),a.begin()+count,b.begin()))
   throw std::runtime_error("Unchanged pack entry bytes differ: "+old.name);
  remaining-=count;
 }
}

void repack(const fs::path&source,const fs::path&destination,const fs::path&encoded,
            const std::pair<Bytes,Bytes>&keys,qlie::PackVersion version,bool extract){
 qlie::Pack archive(source.string(),keys.first,keys.second,version);
 std::map<size_t,Bytes> replacements;
 for(size_t index=0;index<archive.entries.size();++index){
  const auto&entry=archive.entries[index];
  const auto name=qlie::canonical(entry.name);
  const auto extension=fs::path(name).extension();
  if(extension!=".b"&&extension!=".ogv")continue;
  auto replacement=replaceVideo(archive.read(entry),encoded,source.filename().string()+":"+name,extract);
  if(replacement)replacements.emplace(index,std::move(*replacement));
 }
 if(replacements.empty())return;
 require(!fs::exists(destination),"Output already exists");
 std::ifstream original(source,std::ios::binary);
 original.seekg(-8,std::ios::end);
 Bytes pointer(8);
 require(bool(original.read(reinterpret_cast<char*>(pointer.data()),pointer.size())),"Missing pack index pointer");
 const auto oldIndex=readLE(pointer,0,8);
 const auto size=fs::file_size(source);
 require(oldIndex<=size-28,"Pack index outside file");
 Bytes trailer(size-oldIndex);
 original.seekg(oldIndex);
 require(bool(original.read(reinterpret_cast<char*>(trailer.data()),trailer.size())),"Cannot read pack index");
 std::vector<size_t> fields,order(archive.entries.size());
 size_t pos=0;
 for(const auto&entry:archive.entries){
  const auto units=readLE(trailer,pos,2);
  const auto len=units*(version==qlie::PackVersion::v31?2:1);
  require(entry.rawname.size()==len,"QLIE index layout differs");
  fields.push_back(pos+2+len);
  pos+=2+len+28;
 }
 std::iota(order.begin(),order.end(),size_t{0});
 std::sort(order.begin(),order.end(),[&](size_t left,size_t right){
  const auto&a=archive.entries[left];const auto&b=archive.entries[right];
  return std::tie(a.offset,a.size)<std::tie(b.offset,b.size);
 });
 auto temporary=destination;temporary+=".part";
 require(!fs::exists(temporary),"Temporary pack already exists");
 try{
  std::ofstream output(temporary,std::ios::binary);
  require(bool(output),"Cannot create staged QLIE pack");
  std::vector<char> buffer(1024*1024);
  uint64_t cursor=0;
  for(auto index:order){
   const auto&e=archive.entries[index];
   require(cursor<=e.offset,"Overlapping pack entries");
   copy(original,output,cursor,e.offset-cursor,buffer);
   const auto field=fields[index];
   writeLE(trailer,field,uint64_t(output.tellp()),8);
   if(auto replacement=replacements.find(index);replacement!=replacements.end()){
    const auto&payload=replacement->second;
    require(payload.size()<=UINT32_MAX,"Replacement pack entry exceeds 32 bits");
    write(output,payload);
    writeLE(trailer,field+8,payload.size(),4);
    writeLE(trailer,field+12,payload.size(),4);
    writeLE(trailer,field+16,0,4); // The replacement is stored as-is.
    writeLE(trailer,field+20,0,4);
    writeLE(trailer,field+24,archive.checksum(payload),4);
   }else copy(original,output,e.offset,e.size,buffer);
   cursor=e.offset+e.size;
  }
  require(cursor<=oldIndex,"Pack data overlaps the table");
  copy(original,output,cursor,oldIndex-cursor,buffer);
  writeLE(trailer,trailer.size()-8,uint64_t(output.tellp()),8);
  write(output,trailer);
  output.close();
  require(bool(output),"Cannot close staged pack");

  qlie::Pack verified(temporary.string(),keys.first,keys.second,version);
  require(verified.entries.size()==archive.entries.size(),"Repacked entry count changed");
  std::ifstream fresh(temporary,std::ios::binary);
  std::vector<char> readback(buffer.size());
  for(size_t index=0;index<archive.entries.size();++index){
   const auto&entry=verified.entries[index];
   require(entry.rawname==archive.entries[index].rawname,"Repacked entry name changed");
   if(auto replacement=replacements.find(index);replacement!=replacements.end())
    require(verified.read(entry)==replacement->second,"Replacement entry differs on readback");
   else verifyCopy(original,fresh,archive.entries[index],entry,buffer,readback);
  }
  fs::rename(temporary,destination);
 }catch(...){fs::remove(temporary);throw;}
 std::cout<<"PACK\t"<<source.filename().string()<<"\t"<<replacements.size()<<"\t"
          <<archive.entries.size()-replacements.size()<<"\t"<<fs::file_size(destination)<<"\n";
}
}

int main(int argc,char**argv){
 const bool extract=argc==4&&std::string_view(argv[1])=="--extract";
 if(argc!=4){
  std::cerr<<"usage: repack-video GAME_ROOT ENCODED_DIRECTORY OUTPUT_GAMEDATA\n"
           <<"       repack-video --extract GAME_ROOT OUTPUT_OGG_DIRECTORY\n";
  return 2;
 }
 try{
  const fs::path game=argv[extract?2:1],encoded=argv[extract?3:2],output=argv[3];
  fs::create_directories(output);
  require(!fs::equivalent(game/"GameData",output),"Use a separate output directory");
  const auto& layout=game_profile::archives;
  const auto keys=qlie::game_keys(game.string(),layout);
  for(int slot=0;slot<16;++slot){
   const auto name="data"+std::to_string(slot)+".pack";
   const auto source=game/"GameData"/name;
   if(fs::is_regular_file(source))repack(source,output/name,encoded,keys,layout.version,extract);
  }
  return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
