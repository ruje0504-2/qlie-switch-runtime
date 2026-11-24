// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles/whj2.hpp"
#include "title_screen.hpp"
#include <chrono>
#include <iostream>
#include <algorithm>
int main(int argc,char**argv){try{if(argc<2)return 2;qlie::Assets assets(argv[1],whj2::profile::archives);whj2::TitleScreen title(assets);title.reset();qlie::Bytes pixels(1280*720*4);double sum=0;uint64_t hash=1469598103934665603ull;
 for(int i=0;i<60;i++){title.view(i<20?640:i<40?0:1280);title.update(i==0?5500:16.6666667);std::fill(pixels.begin(),pixels.end(),0);auto start=std::chrono::steady_clock::now();title.draw(pixels);double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();sum+=ms;for(auto b:pixels){hash^=b;hash*=1099511628211ull;}std::cout<<"FRAME "<<i<<" ms="<<ms<<" hash="<<hash<<"\n";if(argc>2&&(i==0||i==20||i==59))qlie::write_file(std::string(argv[2])+"-"+std::to_string(i)+".rgba",pixels);}
 std::cout<<"MEAN_MS "<<sum/60<<" HASH "<<hash<<"\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
