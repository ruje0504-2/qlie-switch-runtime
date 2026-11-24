// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles/whj2.hpp"
#include "upper_menu.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1],whj2::profile::archives);whj2::UpperMenu menu(assets);qlie::Bytes frame(1280*720*4);menu.draw(frame);if(std::any_of(frame.begin(),frame.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Hidden toolbar changed scene");
 if(menu.swipe(150,80)||menu.visible())throw std::runtime_error("Horizontal gesture opened toolbar");if(!menu.swipe(10,100)||!menu.visible())throw std::runtime_error("Down swipe did not open toolbar");menu.draw(frame);if(std::none_of(frame.begin(),frame.begin()+1280*100*4,[](uint8_t v){return v!=0;}))throw std::runtime_error("Original toolbar pixels absent");if(std::any_of(frame.begin()+1280*100*4,frame.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Toolbar covered body area");
 if(menu.confirm()!=menu.Quit)throw std::runtime_error("Initial button");menu.move(1);if(menu.confirm()!=menu.Title)throw std::runtime_error("Title button");menu.move(1);if(menu.confirm()!=menu.Screen)throw std::runtime_error("Screen button");if(menu.tap(935,25)!=menu.Quit||menu.tap(1070,25)!=menu.Title||menu.tap(1205,25)!=menu.Screen)throw std::runtime_error("Original button hit bounds");menu.tap(500,300);if(menu.visible())throw std::runtime_error("Outside tap did not close");menu.toggle();menu.toggle();if(menu.visible())throw std::runtime_error("Y toggle equivalent");
 std::cout<<"PASS original toolbar assets/top-only pixels, downward gesture, toggling and three original action targets\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
