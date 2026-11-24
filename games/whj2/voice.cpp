// SPDX-License-Identifier: GPL-2.0-or-later
#include "voice.hpp"
#include "story.hpp"
#include <sstream>
#include <algorithm>
namespace whj2 {
VoiceResolver::VoiceResolver(qlie::Assets&a):assets(a){
 for(auto name:{"avg/setting.txt","avg/projectsetting.txt"}){
  std::istringstream in(qlie::cp932(assets.read(name)));std::string line;
  while(std::getline(in,line)){
   auto p=line.find('=');if(p==line.npos)continue;
   auto key=qlie::canonical(trim(line.substr(0,p))),value=trim(line.substr(p+1));
   if(key=="pathformat")prefix=qlie::canonical(value);
   else if(key=="voicefileerror")fileErrors=value=="1";
   else if(key=="forcevoiceplay")forcePlay=value=="1";
   else if(key=="volumevoice")gain=std::clamp(std::stof(value)/100.f,0.f,1.f);
  }
 }
}
std::vector<std::string> VoiceResolver::candidates(std::string id)const{
 const std::string mark="％";for(size_t p;(p=id.find(mark))!=id.npos;)id.erase(p,mark.size());id=trim(id);
 if(id.empty()||id=="none")return {};
 auto base=prefix+qlie::canonical(id);return {base,base+".ogg"};
}
std::optional<VoiceMatch> VoiceResolver::resolve(const std::string&id)const{
 for(auto&candidate:candidates(id))if(auto source=assets.locate(candidate))return VoiceMatch{candidate,*source};
 return {};
}
}
