// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "qlie.hpp"
#include <array>
#include <map>
#include <string_view>
namespace whj2 {
class Settings {
 std::map<std::string,int> values,defaults;
 bool namedVoices;
public:
 explicit Settings(qlie::Assets&,bool namedVoices=false);
 unsigned voiceActorCount()const{return namedVoices?7:0;}
 float characterVoiceGain(std::string_view actor)const;
 bool characterVoiceMuted(unsigned index)const;
 void toggleCharacterVoice(unsigned index);
 int get(const std::string&)const;
 void set(const std::string&,int);
 void reset(){values=defaults;}
 std::array<float,5> gains()const;
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
