// SPDX-License-Identifier: GPL-2.0-or-later
#include "sentence_wait.hpp"
#include "state_io.hpp"
#include <algorithm>
#include <stdexcept>
namespace whj2 {
namespace {
double waitTime(const std::string& value){
 if(value.empty())return 0;
 return value[0]=='$'?double(std::stoul(value.substr(1),nullptr,16)):std::stod(value);
}
}
bool SentenceWait::property(const std::vector<std::string>&f){if(f.empty())return false;
 if(f[0]=="autowait"){automatic=f.size()>1&&f[1]=="true";return true;}
 if(f[0]!="wait")return false;
 mode=f.size()>1?f[1]:"message";duration=f.size()>2?waitTime(f[2]):0;
 skipDuration=f.size()>3&&!f[3].empty()?waitTime(f[3]):duration;
 if(mode!="message"&&mode!="click"&&mode!="wait"&&mode!="clickonly"&&mode!="waitonly"&&mode!="skip"&&mode!="voice")throw std::runtime_error("Unsupported sentence wait mode "+mode);
 return true;
}
void SentenceWait::fromFade(double ms){if(automatic){mode="click";duration=std::max(0.,ms);skipDuration=duration;}}
void SentenceWait::cancel(){stage=Stage::Idle;remaining=skipRemaining=0;message=continuation=voiceBound=false;}
void SentenceWait::page(bool hasText,bool voicePlaying){cancel();
 skipRemaining=std::max(0.,skipDuration);skipDuration=0;message=hasText&&(mode=="message"||mode=="click"||mode=="wait");interruptible=mode=="click"||mode=="clickonly"||mode=="voice";
 remaining=mode=="message"||mode=="skip"?0:std::max(0.,duration);voiceBound=mode=="voice"&&voicePlaying;if(mode=="voice"&&!voicePlaying)remaining=250;
 stage=remaining>0?Stage::Delay:message?Stage::Message:Stage::Ready;
}
void SentenceWait::native(bool clickable,double ms){cancel();continuation=true;interruptible=clickable;remaining=std::max(0.,ms);stage=remaining>0?Stage::Delay:Stage::Ready;}
int SentenceWait::finishDelay(){if(message){stage=Stage::Message;return None;}auto action=continuation?Resume:Next;cancel();return action;}
SentenceWait::Action SentenceWait::tick(double ms,bool voicePlaying){if(stage==Stage::Ready)return Action(finishDelay());if(stage!=Stage::Delay)return None;
 remaining=std::max(0.,remaining-std::max(0.,ms));if(voiceBound&&!voicePlaying)remaining=0;if(remaining==0)return Action(finishDelay());return None;
}
SentenceWait::Action SentenceWait::confirm(){if(stage==Stage::Delay){if(!interruptible)return None;remaining=0;return Action(finishDelay());}if(stage==Stage::Message){cancel();return Next;}if(stage==Stage::Ready)return Action(finishDelay());return None;}
SentenceWait::Action SentenceWait::force(){if(stage==Stage::Idle)return None;auto result=continuation?Resume:Next;cancel();return result;}
void SentenceWait::elapseSkip(double milliseconds){
 skipRemaining=std::max(0.,skipRemaining-std::max(0.,milliseconds));
}
SentenceWait::Action SentenceWait::skip(){return skipRemaining>0?None:force();}
qlie::Bytes SentenceWait::save()const{StateWriter w;w.str("WHJ2-wait-2");w.str(mode);w.f(float(duration));w.f(float(remaining));w.n(unsigned(stage));w.n(automatic);w.n(interruptible);w.n(message);w.n(continuation);w.n(voiceBound);w.f(float(skipDuration));w.f(float(skipRemaining));return w.bytes;}
void SentenceWait::restore(const qlie::Bytes&b){StateReader r{b};auto version=r.str();if(version!="WHJ2-wait-1"&&version!="WHJ2-wait-2")throw std::runtime_error("Wait state version");mode=r.str();duration=r.f();remaining=r.f();auto value=r.n();if(value>3)throw std::runtime_error("Wait stage");stage=Stage(value);automatic=r.n()!=0;interruptible=r.n()!=0;message=r.n()!=0;continuation=r.n()!=0;voiceBound=r.n()!=0;skipDuration=skipRemaining=0;if(version=="WHJ2-wait-2"){skipDuration=r.f();skipRemaining=r.f();}r.end();}
}
