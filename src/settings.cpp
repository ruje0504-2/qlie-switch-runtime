// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.hpp"
#include "state_io.hpp"
#include "story.hpp"
#include <sstream>
#include <algorithm>
#include <stdexcept>
namespace whj2 {
Settings::Settings(qlie::Assets&a){values={{"MasterVolume",100},{"BGMVolume",60},{"VoiceVolume",100},{"SEVolume",100},{"SysSEVolume",50},{"MovieVolume",80},{"WindowAlpha",210},{"TextSpeed",10},{"ClickVoiceStop",0},{"AutoPlayVoiceWait",1},{"AutoTextWait",40},{"AutoClickWait",1200}};
 std::istringstream in(qlie::cp932(a.read("script/config/default.txt")));std::string line;while(std::getline(in,line)){auto pos=line.find('=');if(pos==line.npos)continue;auto key=trim(line.substr(0,pos));if(values.count(key))set(key,std::stoi(trim(line.substr(pos+1))));}defaults=values;
}
int Settings::get(const std::string&key)const{return values.at(key);}
void Settings::set(const std::string&key,int value){if(!values.count(key))throw std::runtime_error("Unknown settings key "+key);values[key]=std::clamp(value,0,key=="WindowAlpha"?255:(key=="ClickVoiceStop"||key=="AutoPlayVoiceWait")?1:key=="AutoClickWait"?5000:100);}
std::array<float,5> Settings::gains()const{std::array<float,5> out;const char*keys[]={"BGMVolume","VoiceVolume","SEVolume","SysSEVolume","MovieVolume"};for(unsigned i=0;i<5;i++)out[i]=float((get(keys[i])*get("MasterVolume")/100)*256/100)/256;return out;}
qlie::Bytes Settings::save()const{StateWriter w;w.str("WHJ2-settings-1");w.n(values.size());for(auto&item:values){w.str(item.first);w.n(item.second);}return w.bytes;}
void Settings::restore(const qlie::Bytes&bytes){StateReader r{bytes};if(r.str()!="WHJ2-settings-1")throw std::runtime_error("Settings version");auto count=r.n();auto saved=values;for(uint64_t i=0;i<count;i++){auto key=r.str();auto value=r.n();if(!saved.count(key)||value>uint64_t(key=="WindowAlpha"?255:(key=="ClickVoiceStop"||key=="AutoPlayVoiceWait")?1:key=="AutoClickWait"?5000:100))throw std::runtime_error("Invalid settings value");saved[key]=int(value);}r.end();values=std::move(saved);}
}
