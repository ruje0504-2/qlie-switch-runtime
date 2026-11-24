// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <SDL.h>
#include <algorithm>
#include <limits>
namespace whj2 {
class FramePacer {
 Uint64 frequency=SDL_GetPerformanceFrequency(),previous=0,minimum=std::numeric_limits<Uint64>::max();
public:
 static constexpr unsigned Limit=30;
 void wait(){Uint64 now=SDL_GetPerformanceCounter();if(previous){const Uint64 period=(frequency+Limit-1)/Limit;while(now-previous<period){auto remaining=period-(now-previous);Uint64 delay=(remaining*1000+frequency-1)/frequency;SDL_Delay(Uint32(std::max<Uint64>(delay,1)));now=SDL_GetPerformanceCounter();}minimum=std::min(minimum,now-previous);}previous=now;}
 double minimumMilliseconds()const{return minimum==std::numeric_limits<Uint64>::max()?0:1000.*minimum/frequency;}
};
}
