// SPDX-License-Identifier: GPL-2.0-or-later
#include "settings.hpp"
#include "game_menu.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1]);whj2::Settings settings(assets);
 if(settings.get("WindowAlpha")!=210||settings.get("TextSpeed")!=10||settings.get("BGMVolume")!=60)throw std::runtime_error("Original defaults");
 auto gains=settings.gains();if(gains[0]!=153.f/256||gains[3]!=128.f/256||gains[4]!=204.f/256)throw std::runtime_error("Original category quantization");
 settings.set("MasterVolume",33);if(settings.gains()[0]!=48.f/256)throw std::runtime_error("Master/category integer order");
 settings.reset();whj2::GameMenu menu(assets,settings);menu.open(whj2::GameMenu::Settings);menu.move(1,0);if(settings.get("WindowAlpha")!=216)throw std::runtime_error("Controller slider");
 if(menu.tap(598,314)!=whj2::GameMenu::SettingsChanged||settings.get("TextSpeed")!=0)throw std::runtime_error("Speed slider right edge");
 menu.tap(357,314);if(settings.get("TextSpeed")!=100)throw std::runtime_error("Speed slider left edge");
 menu.tap(664,163);if(settings.gains()[0]!=0)throw std::runtime_error("Master mute");
 auto state=settings.save();whj2::Settings restored(assets);restored.restore(state);if(restored.save()!=state)throw std::runtime_error("Settings roundtrip");
 qlie::Bytes pixels(1280*720*4);menu.draw(pixels);if(std::none_of(pixels.begin(),pixels.end(),[](uint8_t x){return x!=0;}))throw std::runtime_error("Original controls not rendered");
 std::cout<<"PASS original defaults and fixed point gains; slider endpoints/controller; settings persistence; original artwork\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
