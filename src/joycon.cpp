// SPDX-License-Identifier: GPL-2.0-or-later
#include "joycon.hpp"
#ifdef __SWITCH__
namespace whj2 {
JoyCon::JoyCon(){padConfigureInput(1,HidNpadStyleTag_NpadHandheld|HidNpadStyleTag_NpadJoyDual|HidNpadStyleTag_NpadJoyLeft|HidNpadStyleTag_NpadJoyRight);
 padInitializeDefault(&pad);
 }
JoyConInput JoyCon::poll(uint64_t ms){
 padUpdate(&pad);
 uint64_t down=padGetButtonsDown(&pad),held=padGetButtons(&pad);
 JoyConInput out;
 out.confirm=down&(HidNpadButton_A|HidNpadButton_AnySL);
 out.back=down&(HidNpadButton_B|HidNpadButton_AnySR);
 out.save=down&(HidNpadButton_X|HidNpadButton_L|HidNpadButton_R);
 out.load=down&(HidNpadButton_Y|HidNpadButton_ZL|HidNpadButton_ZR);
 out.menu=down&(HidNpadButton_Plus|HidNpadButton_Minus);
 out.hide=down&(HidNpadButton_StickL|HidNpadButton_StickR);
 int direction=(held&HidNpadButton_AnyUp)?-1:(held&HidNpadButton_AnyDown)?1:0;
 if(direction&&(direction!=heldDirection||ms>=repeatAt)){out.direction=direction;
 repeatAt=ms+(direction!=heldDirection?350:90);
 }heldDirection=direction;
 return out;
}
}
#endif
