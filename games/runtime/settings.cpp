// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.hpp"
#include "state_io.hpp"
#include "story.hpp"
#include <sstream>
#include <algorithm>
#include <stdexcept>
namespace whj2 {
namespace {
constexpr const char* actors[]={"莲华","香恋","萌世歌","春","皇","舞斗","etc"};
std::string muteKey(unsigned index){return std::string("CharacterVoiceMute.")+actors[index];}
std::string gainKey(unsigned index){return std::string("CharacterVoiceGain.")+actors[index];}
int settingMaximum(const std::string&key){
 if(key=="WindowAlpha")return 255;
 if(key.rfind("CharacterVoiceMute.",0)==0)return 1;
 if(key.rfind("CharacterVoiceGain.",0)==0)return 256;
 if(key=="SkipMode"||key=="ClickVoiceStop"||key=="AutoPlayVoiceWait")return 1;
 return key=="AutoClickWait"?5000:100;
}
}

Settings::Settings(qlie::Assets&a,bool actorSettings):namedVoices(actorSettings){values={{"SkipMode",1},{"MasterVolume",100},{"MusicModeVolume",100},{"BGMVolume",60},{"VoiceVolume",100},{"SEVolume",100},{"SysSEVolume",50},{"MovieVolume",80},{"WindowAlpha",210},{"TextSpeed",10},{"ClickVoiceStop",0},{"AutoPlayVoiceWait",1},{"AutoTextWait",40},{"AutoClickWait",1200}};
 for(unsigned i=0;i<voiceActorCount();++i){values.emplace(muteKey(i),0);values.emplace(gainKey(i),256);}
 std::istringstream in(a.text("script/config/default.txt"));std::string line;while(std::getline(in,line)){auto pos=line.find('=');if(pos==line.npos)continue;auto key=trim(line.substr(0,pos));if(values.count(key)){auto value=trim(line.substr(pos+1));set(key,std::stoi(value[0]=='$'?value.substr(1):value,nullptr,value[0]=='$'?16:10));}}defaults=values;
}
float Settings::characterVoiceGain(std::string_view actor)const{
 if(!namedVoices||actor=="none")return 1;
 if(actor=="蓮華")actor="莲华";
 if(actor=="もよか")actor="萌世歌";
 if(actor=="はる")actor="春";
 if(actor=="公暁")actor="皇";
 const auto end=std::end(actors)-1;
 const unsigned index=unsigned(std::find(std::begin(actors),end,actor)-std::begin(actors));
 return characterVoiceMuted(index)?0.f:float(get(gainKey(index)))/256;
}
bool Settings::characterVoiceMuted(unsigned index)const{return get(muteKey(index))!=0;}
void Settings::toggleCharacterVoice(unsigned index){set(muteKey(index),!characterVoiceMuted(index));}
int Settings::get(const std::string&key)const{return values.at(key);}
void Settings::set(const std::string&key,int value){if(!values.count(key))throw std::runtime_error("Unknown settings key "+key);values[key]=std::clamp(value,0,settingMaximum(key));}
std::array<float,5> Settings::gains()const{std::array<float,5> out;const char*keys[]={"BGMVolume","VoiceVolume","SEVolume","SysSEVolume","MovieVolume"};for(unsigned i=0;i<5;i++)out[i]=float((get(keys[i])*get("MasterVolume")/100)*256/100)/256;return out;}
qlie::Bytes Settings::save()const{StateWriter w;w.str("WHJ2-settings-1");w.n(values.size());for(auto&item:values){w.str(item.first);w.n(item.second);}return w.bytes;}
void Settings::restore(const qlie::Bytes&bytes){
 StateReader r{bytes};if(r.str()!="WHJ2-settings-1")throw std::runtime_error("Settings version");
 auto count=r.n();auto saved=values;
 for(uint64_t i=0;i<count;i++){
  auto key=r.str();auto value=r.n();
  // Older settings include the display mode and font controls.
  if(key=="Interlace"||key=="FontAA"||key=="FontStyle"||key=="FontStyleColor")continue;
  if(!saved.count(key)||value>uint64_t(settingMaximum(key)))throw std::runtime_error("Invalid settings value");
  saved[key]=int(value);
 }
 r.end();values=std::move(saved);
}
}
