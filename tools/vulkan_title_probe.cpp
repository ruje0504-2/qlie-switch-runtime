// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles/whj2.hpp"
#include "title_screen.hpp"
#include "vulkan_raster.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>
int main(int argc,char**argv){try{if(argc<2)return 2;qlie::Assets assets(argv[1],whj2::profile::archives);whj2::TitleScreen title(assets);title.reset();whj2::VulkanRaster gpu;qlie::Bytes cpu(1280*720*4),pixels(cpu.size());double total=0;size_t differences=0;unsigned maximum=0;
 int frames=argc>2?std::stoi(argv[2]):60;for(int i=0;i<frames;i++){title.view(i<20?640:i<40?0:1280);title.update(i==0?5500:16.6666667);std::fill(cpu.begin(),cpu.end(),0);std::fill(pixels.begin(),pixels.end(),0);title.draw(cpu);auto start=std::chrono::steady_clock::now();gpu.begin(pixels);title.draw(gpu);gpu.end(pixels);double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();total+=ms;size_t diff=0;unsigned maxdiff=0;for(size_t n=0;n<cpu.size();n++){unsigned d=unsigned(std::abs(int(cpu[n])-int(pixels[n])));diff+=d!=0;maxdiff=std::max(maxdiff,d);}if(i==0){qlie::write_file("reports/vulkan-title-cpu.rgba",cpu);qlie::write_file("reports/vulkan-title-gpu.rgba",pixels);unsigned shown=0;for(size_t n=0;n<cpu.size()&&shown<16;n++)if(cpu[n]!=pixels[n]){std::cout<<"DIFF x="<<(n/4%1280)<<" y="<<(n/4/1280)<<" channel="<<(n%4)<<" cpu="<<int(cpu[n])<<" gpu="<<int(pixels[n])<<std::endl;shown++;}}differences+=diff;maximum=std::max(maximum,maxdiff);std::cout<<"FRAME "<<i<<" ms="<<ms<<" different="<<diff<<" max="<<maxdiff<<std::endl;}
 std::cout<<"GPU_MEAN_MS "<<total/frames<<" differences="<<differences<<" maximum="<<maximum<<std::endl;return maximum>2?1:0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<std::endl;return 1;}}
