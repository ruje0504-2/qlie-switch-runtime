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
 out.save=down&HidNpadButton_L;
 out.load=down&HidNpadButton_R;
 out.fastForward=held&HidNpadButton_ZR;
 out.history=down&HidNpadButton_Up;
 out.horizontal=(down&HidNpadButton_AnyLeft)?-1:(down&HidNpadButton_AnyRight)?1:0;
 out.upperMenu=down&HidNpadButton_Y;
 out.fps=down&HidNpadButton_Minus;
 out.menu=down&HidNpadButton_X;
 int direction=(held&HidNpadButton_AnyUp)?-1:(held&HidNpadButton_AnyDown)?1:0;
 if(direction&&(direction!=heldDirection||ms>=repeatAt)){out.direction=direction;
 repeatAt=ms+(direction!=heldDirection?350:90);
 }heldDirection=direction;
 return out;
}
}
#endif
