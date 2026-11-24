// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "qlie.hpp"
namespace whj2 {
class AutoPlay {
 double remaining=0;size_t counted=0;
public:
 bool enabled=false;
 void page(double clickWait){remaining=clickWait;counted=0;}
 void tick(double milliseconds,size_t visible,double textWait,double textSpeed);
 bool ready(bool textFinished,bool voicePlaying,bool waitVoice)const{return enabled&&textFinished&&remaining<=0&&(!waitVoice||!voicePlaying);}
 double pending()const{return remaining;}
 qlie::Bytes save()const;void restore(const qlie::Bytes&);
};
}
