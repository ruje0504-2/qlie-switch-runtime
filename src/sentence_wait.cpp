// SPDX-License-Identifier: GPL-2.0-or-later
#include "sentence_wait.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <stdexcept>
namespace whj2 {
bool SentenceWait::property(const std::vector<std::string>&f){if(f.empty())return false;
 if(f[0]=="autowait"){automatic=f.size()>1&&f[1]=="true";return true;}
 if(f[0]!="wait")return false;
 mode=f.size()>1?f[1]:"message";duration=f.size()>2&&!f[2].empty()?(f[2][0]=='$'?double(std::stoul(f[2].substr(1),nullptr,16)):std::stod(f[2])):0;
 if(mode!="message"&&mode!="click"&&mode!="wait"&&mode!="clickonly"&&mode!="waitonly"&&mode!="skip"&&mode!="voice")throw std::runtime_error("Unsupported sentence wait mode "+mode);
 return true;
}
void SentenceWait::fromFade(double ms){if(automatic){mode="click";duration=std::max(0.,ms);}}
void SentenceWait::cancel(){stage=Stage::Idle;remaining=0;message=continuation=voiceBound=false;}
void SentenceWait::page(bool hasText,bool voicePlaying){cancel();message=hasText&&(mode=="message"||mode=="click"||mode=="wait");interruptible=mode=="click"||mode=="clickonly"||mode=="voice";
 remaining=mode=="message"||mode=="skip"?0:std::max(0.,duration);voiceBound=mode=="voice"&&voicePlaying;if(mode=="voice"&&!voicePlaying)remaining=250;
 stage=remaining>0?Stage::Delay:message?Stage::Message:Stage::Ready;
}
void SentenceWait::native(bool clickable,double ms){cancel();continuation=true;interruptible=clickable;remaining=std::max(0.,ms);stage=remaining>0?Stage::Delay:Stage::Ready;}
int SentenceWait::finishDelay(){if(message){stage=Stage::Message;return None;}auto action=continuation?Resume:Next;cancel();return action;}
SentenceWait::Action SentenceWait::tick(double ms,bool voicePlaying){if(stage==Stage::Ready)return Action(finishDelay());if(stage!=Stage::Delay)return None;
 remaining=std::max(0.,remaining-std::max(0.,ms));if(voiceBound&&!voicePlaying)remaining=0;if(remaining==0)return Action(finishDelay());return None;
}
SentenceWait::Action SentenceWait::confirm(){if(stage==Stage::Delay){if(!interruptible)return None;remaining=0;return Action(finishDelay());}if(stage==Stage::Message){cancel();return Next;}if(stage==Stage::Ready)return Action(finishDelay());return None;}
qlie::Bytes SentenceWait::save()const{StateWriter w;w.str("WHJ2-wait-1");w.str(mode);w.f(float(duration));w.f(float(remaining));w.n(unsigned(stage));w.n(automatic);w.n(interruptible);w.n(message);w.n(continuation);w.n(voiceBound);return w.bytes;}
void SentenceWait::restore(const qlie::Bytes&b){StateReader r{b};if(r.str()!="WHJ2-wait-1")throw std::runtime_error("Wait state version");mode=r.str();duration=r.f();remaining=r.f();auto value=r.n();if(value>3)throw std::runtime_error("Wait stage");stage=Stage(value);automatic=r.n()!=0;interruptible=r.n()!=0;message=r.n()!=0;continuation=r.n()!=0;voiceBound=r.n()!=0;r.end();}
}
