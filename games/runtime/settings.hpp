// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "qlie.hpp"
#include <array>
#include <map>
#include <string_view>
#ifdef QLIE_PROFILE_WHJ6
#include "profiles/whj6.hpp"
#endif
namespace whj2 {
class Settings {
 std::map<std::string,int> values,defaults;
 bool namedVoices;
public:
 explicit Settings(qlie::Assets&,bool namedVoices=false,bool waitUsesZero=false);
 unsigned voiceActorCount()const{
#ifdef QLIE_PROFILE_WHJ6
  return namedVoices?std::size(whj6::profile::settingsCast):0;
#else
  return namedVoices?7:0;
#endif
 }
 float characterVoiceGain(std::string_view actor)const;
 bool characterVoiceMuted(unsigned index)const;
 void toggleCharacterVoice(unsigned index);
#ifdef QLIE_PROFILE_WHJ6
 int characterVoiceLevel(unsigned index)const;
 void setCharacterVoiceLevel(unsigned index,int level);
#endif
 int get(const std::string&)const;
 void set(const std::string&,int);
 void reset(){values=defaults;}
 std::array<float,5> gains()const;
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
