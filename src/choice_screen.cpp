// SPDX-License-Identifier: GPL-2.0-or-later
#include "choice_screen.hpp"
#include <cmath>
#include <stdexcept>
namespace whj2 {
void ChoiceScreen::show(const std::vector<std::string>&values,KFont*font){if(values.empty()||values.size()>6)throw std::runtime_error("Choice count outside original layout");entries=values;elapsed=0;selected=0;scene.clear();scene.command("^ui_choice_animation,file:avg/select/animation.b,x:100,y:600,pri:1500,motion:*Enable");
 int top=290-int(entries.size()-1)*30;for(size_t i=0;i<entries.size();i++){auto name="ui_choice"+std::to_string(i);scene.command("^"+name+",file:avg/select/selectbutton.b,x:640,y:"+std::to_string(top+int(i)*60)+",pri:1520,motion:*Enable");MessageText text(assets);text.start("[spd,0][pc,"+entries[i]+"]");scene.uiSlot(name,2,text.render(font,MessageStyle{540,50,24,24,0xffffff}));}select(0);
}
bool ChoiceScreen::select(int index){if(index<0||size_t(index)>=entries.size())return false;int old=selected;selected=index;if(old!=selected)scene.command("^ui_choice"+std::to_string(old)+",motion:*OffMouse");scene.command("^ui_choice"+std::to_string(selected)+",motion:*OnMouse");return old!=selected;}
int ChoiceScreen::hit(float x,float y)const{if(!ready())return -1;for(size_t i=0;i<entries.size();i++)if(scene.uiHit("ui_choice"+std::to_string(i),x,y))return int(i);return -1;}
}
