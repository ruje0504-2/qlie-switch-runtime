// SPDX-License-Identifier: GPL-2.0-or-later
#include "replay_screen.hpp"
#include <iostream>
#include <algorithm>
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1]);whj2::Progress progress;whj2::ReplayScreen screen(assets,progress);screen.open();screen.update(500);
 if(screen.confirm()!=whj2::ReplayScreen::None)throw std::runtime_error("Empty progress unlocked replay");
 screen.move(0,-1);screen.move(1,0);screen.confirm();screen.update(500);if(screen.pageIndex()!=1||screen.selection()!=0)throw std::runtime_error("Controller second page");
 if(screen.confirm()!=whj2::ReplayScreen::None)throw std::runtime_error("Last page lock failed");
 if(screen.tap(1210,695)!=whj2::ReplayScreen::Back)throw std::runtime_error("Touch back button");
 whj2::Story story(assets);story.start();const std::string key="scenario/本編/w@03_02.s:@@replay01";
 for(unsigned i=0;i<100000&&!story.visited.count(key);i++){auto e=story.next();if(e.type=="choice")story.choose(0);if(e.type=="finish"||e.type=="finished")throw std::runtime_error("Did not reach first replay label");}
 if(!story.visited.count(key))throw std::runtime_error("Replay unlock budget");progress.capture(story);screen.open();screen.update(500);if(screen.confirm()!=0)throw std::runtime_error("Earned replay did not unlock");
 qlie::Bytes pixels(1280*720*4);screen.draw(pixels);if(std::count_if(pixels.begin(),pixels.end(),[](uint8_t x){return x!=0;})<500000)throw std::runtime_error("Replay frame incomplete");
 std::cout<<"PASS locked catalogue / controller page2 / touch back / naturally reached label unlock / frame output\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
