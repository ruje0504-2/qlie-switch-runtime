// SPDX-License-Identifier: GPL-2.0-or-later
#include "joycon.hpp"
#include <cstdlib>
#ifdef __SWITCH__
namespace whj2 {
JoyCon::JoyCon(){padConfigureInput(1,HidNpadStyleTag_NpadHandheld|HidNpadStyleTag_NpadJoyDual|HidNpadStyleTag_NpadJoyLeft|HidNpadStyleTag_NpadJoyRight);
 padInitializeDefault(&pad);
 }
JoyConInput JoyCon::poll(uint64_t ms){
 padUpdate(&pad);
 uint64_t down=padGetButtonsDown(&pad),held=padGetButtons(&pad);
 JoyConInput out;auto stick=padGetStickPos(&pad,0);out.viewAxis=std::abs(stick.x)>6000?stick.x/32767.f:0;out.viewAxisY=std::abs(stick.y)>6000?-stick.y/32767.f:0;
 out.confirm=down&(HidNpadButton_A|HidNpadButton_AnySL);
 out.back=down&(HidNpadButton_B|HidNpadButton_AnySR);
 out.save=down&HidNpadButton_L;
 out.load=down&HidNpadButton_R;
 out.fastForward=held&HidNpadButton_ZR;
 out.history=down&HidNpadButton_Up;
 out.horizontal=(down&HidNpadButton_Left)?-1:(down&HidNpadButton_Right)?1:0;
 out.upperMenu=down&HidNpadButton_Y;
 out.fps=down&HidNpadButton_Minus;
 out.menu=down&HidNpadButton_X;
 int direction=(held&HidNpadButton_Up)?-1:(held&HidNpadButton_Down)?1:0;
 if(direction&&(direction!=heldDirection||ms>=repeatAt)){out.direction=direction;
 repeatAt=ms+(direction!=heldDirection?350:90);
 }heldDirection=direction;
 return out;
}
}
#endif
