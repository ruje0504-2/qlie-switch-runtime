// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.hpp"
#include "state_io.hpp"
#include "story.hpp"
#include <sstream>
#include <algorithm>
#include <stdexcept>
namespace whj2 {
namespace {
int settingMaximum(const std::string&key){
 if(key.rfind("VoiceMute_",0)==0)return 1;
 if(key=="FontAA")return 3;
 if(key=="FontStyle")return 15;
 if(key=="FontStyleColor")return 0xffffff;
 if(key=="WindowAlpha")return 255;
 if(key=="Interlace"||key=="SkipMode"||key=="ClickVoiceStop"||key=="AutoPlayVoiceWait")return 1;
 return key=="AutoClickWait"?5000:100;
}
}

Settings::Settings(qlie::Assets&a){values={{"FontStyle",5},{"FontStyleColor",0},{"FontAA",2},{"Interlace",0},{"SkipMode",1},{"MasterVolume",100},{"MusicModeVolume",100},{"BGMVolume",60},{"VoiceVolume",100},{"SEVolume",100},{"SysSEVolume",50},{"MovieVolume",80},{"WindowAlpha",210},{"TextSpeed",10},{"ClickVoiceStop",0},{"AutoPlayVoiceWait",1},{"AutoTextWait",40},{"AutoClickWait",1200}};
 std::istringstream groups(qlie::cp932(a.read("system/voice/voicedata.dat")));std::string groupLine;
 std::map<unsigned,std::string> ordered;
 while(std::getline(groups,groupLine)){groupLine=trim(groupLine);if(groupLine.rfind("act[",0)!=0)continue;auto close=groupLine.find(']'),eq=groupLine.find('=',close);if(close==groupLine.npos||eq==groupLine.npos)continue;ordered[unsigned(std::stoul(groupLine.substr(4,close-4)))]=trim(groupLine.substr(eq+1));}
 for(const auto&entry:ordered){auto aliases=split(entry.second,',');if(aliases.empty())continue;auto name=aliases.front()=="*"?std::string("その他"):aliases.front();voiceGroups.push_back({name,aliases});values["VoiceMute_"+name]=0;}
 std::istringstream in(qlie::cp932(a.read("script/config/default.txt")));std::string line;while(std::getline(in,line)){auto pos=line.find('=');if(pos==line.npos)continue;auto key=trim(line.substr(0,pos));if(values.count(key)){auto value=trim(line.substr(pos+1));set(key,std::stoi(value[0]=='$'?value.substr(1):value,nullptr,value[0]=='$'?16:10));}}defaults=values;
}
int Settings::get(const std::string&key)const{return values.at(key);}
bool Settings::voiceMuted(std::string speaker)const{
 if(speaker.rfind("【",0)==0){speaker.erase(0,3);auto end=speaker.find("】");if(end!=speaker.npos)speaker.resize(end);}
 auto alias=speaker.find("＠");if(alias!=speaker.npos)speaker.resize(alias);
 for(const auto&group:voiceGroups)for(const auto&name:group.second)if(name==speaker||name=="*")return get("VoiceMute_"+group.first)!=0;
 return false;
}
void Settings::set(const std::string&key,int value){if(!values.count(key))throw std::runtime_error("Unknown settings key "+key);values[key]=std::clamp(value,0,settingMaximum(key));}
std::array<float,5> Settings::gains()const{std::array<float,5> out;const char*keys[]={"BGMVolume","VoiceVolume","SEVolume","SysSEVolume","MovieVolume"};for(unsigned i=0;i<5;i++)out[i]=float((get(keys[i])*get("MasterVolume")/100)*256/100)/256;return out;}
qlie::Bytes Settings::save()const{StateWriter w;w.str("WHJ2-settings-1");w.n(values.size());for(auto&item:values){w.str(item.first);w.n(item.second);}return w.bytes;}
void Settings::restore(const qlie::Bytes&bytes){StateReader r{bytes};if(r.str()!="WHJ2-settings-1")throw std::runtime_error("Settings version");auto count=r.n();auto saved=values;for(uint64_t i=0;i<count;i++){auto key=r.str();auto value=r.n();if(!saved.count(key)||value>uint64_t(settingMaximum(key)))throw std::runtime_error("Invalid settings value");saved[key]=int(value);}r.end();values=std::move(saved);}
}
