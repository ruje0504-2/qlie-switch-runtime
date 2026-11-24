// SPDX-License-Identifier: GPL-2.0-or-later
#include "frame_pacer.hpp"
#include <iostream>
int main(){if(SDL_Init(SDL_INIT_TIMER))return 1;whj2::FramePacer pacer;auto start=SDL_GetPerformanceCounter();for(unsigned i=0;i<61;i++)pacer.wait();double elapsed=double(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency();double minimum=pacer.minimumMilliseconds();SDL_Quit();if(elapsed<1||minimum<1000./60){std::cerr<<"FAIL frame cap "<<elapsed<<" "<<minimum<<"\n";return 1;}std::cout<<"PASS 60 FPS cap elapsed="<<elapsed<<" min_interval_ms="<<minimum<<"\n";return 0;}
