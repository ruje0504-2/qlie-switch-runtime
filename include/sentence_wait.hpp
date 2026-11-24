#pragma once
#include "qlie.hpp"
namespace whj2 {
class SentenceWait {
 enum class Stage {Idle,Delay,Message,Ready};
 Stage stage=Stage::Idle;
 std::string mode="message";
 double duration=0,remaining=0,skipDuration=0,skipRemaining=0;
 bool automatic=false,interruptible=false,message=false,continuation=false,voiceBound=false;
 int finishDelay();
public:
 enum Action {None=0,Next=1,Resume=2};
 bool property(const std::vector<std::string>&);
 void resetProperties();
 void fromFade(double);void page(bool hasText=true,bool voicePlaying=false);void native(bool clickable,double milliseconds);
 Action tick(double milliseconds,bool voicePlaying=false);Action confirm();Action force();void cancel();
 void elapseSkip(double milliseconds);
 Action skip();
 double remainingForSkip()const{return skipRemaining;}
 bool waitingForMessage()const{return stage==Stage::Message;}
 bool blocksClick()const{return stage==Stage::Delay&&!interruptible;}
 bool prewaiting()const{return stage==Stage::Delay;}
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
