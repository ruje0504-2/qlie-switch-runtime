#pragma once
#include <cstdint>
#ifdef __SWITCH__
#include <switch.h>
namespace whj2 {
struct JoyConInput {int direction=0,horizontal=0;bool confirm=false,back=false,save=false,load=false,menu=false,history=false,fastForward=false,upperMenu=false;};
class JoyCon {
 PadState pad{};int heldDirection=0;uint64_t repeatAt=0;
public:
 JoyCon();JoyConInput poll(uint64_t milliseconds);
};
}
#endif
