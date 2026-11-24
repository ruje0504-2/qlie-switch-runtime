// SPDX-License-Identifier: GPL-2.0-or-later
#include "whj2/profile.hpp"
#include "settings.hpp"
#include "game_menu.hpp"
#include <iostream>
#include <algorithm>
#include <stdexcept>
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1],whj2::profile::archives);whj2::Settings settings(assets);
 if(settings.get("WindowAlpha")!=210||settings.get("TextSpeed")!=10||settings.get("BGMVolume")!=60)throw std::runtime_error("Original defaults");
 auto gains=settings.gains();if(gains[0]!=153.f/256||gains[3]!=128.f/256||gains[4]!=204.f/256)throw std::runtime_error("Original category quantization");
 settings.set("MasterVolume",33);if(settings.gains()[0]!=48.f/256)throw std::runtime_error("Master/category integer order");
 settings.reset();whj2::GameMenu menu(assets,settings);menu.open(whj2::GameMenu::Settings);menu.update(250);menu.move(1,0);if(settings.get("WindowAlpha")!=216)throw std::runtime_error("Controller slider");
 if(menu.tap(598,314)!=whj2::GameMenu::SettingsChanged||settings.get("TextSpeed")!=0)throw std::runtime_error("Speed slider right edge");
 menu.tap(357,314);if(settings.get("TextSpeed")!=100)throw std::runtime_error("Speed slider left edge");
 menu.tap(664,163);if(settings.gains()[0]!=0)throw std::runtime_error("Master mute");
 if(!menu.beginDrag(664,222)||!menu.drag(2000,20)||settings.get("BGMVolume")!=100)throw std::runtime_error("Captured drag upper clamp");menu.drag(0,700);if(settings.get("BGMVolume")!=0)throw std::runtime_error("Captured drag lower clamp");menu.endDrag();if(menu.drag(1100,222))throw std::runtime_error("Released drag still active");
 menu.tap(598,373);if(settings.get("AutoTextWait")!=0||settings.get("AutoClickWait")!=0)throw std::runtime_error("Auto speed fast endpoint");menu.tap(357,373);if(settings.get("AutoTextWait")!=100||settings.get("AutoClickWait")!=5000)throw std::runtime_error("Auto speed slow endpoint");menu.tap(410,353);if(!settings.get("ClickVoiceStop"))throw std::runtime_error("Click voice setting");menu.tap(534,412);if(settings.get("AutoPlayVoiceWait"))throw std::runtime_error("Voice wait setting");menu.focus=14;menu.move(1,0);if(!settings.get("AutoPlayVoiceWait"))throw std::runtime_error("Controller voice wait setting");
 auto state=settings.save();whj2::Settings restored(assets);restored.restore(state);if(restored.save()!=state)throw std::runtime_error("Settings roundtrip");
 qlie::Bytes pixels(1280*720*4);menu.draw(pixels);if(std::none_of(pixels.begin(),pixels.end(),[](uint8_t x){return x!=0;}))throw std::runtime_error("Original controls not rendered");
 menu.open(whj2::GameMenu::History);menu.update(250);
 const auto pixel=(200*1280+100)*4;
 std::array<uint8_t,3> expected{};
 const std::array<uint8_t,3> tint={32,19,0};
 for(unsigned c=0;c<tint.size();++c)expected[c]=whj2::blendChannel(0,tint[c],100,191);
 std::fill(pixels.begin(),pixels.end(),100);menu.draw(pixels);
 for(unsigned c=0;c<expected.size();++c)if(pixels[pixel+c]!=expected[c])throw std::runtime_error("History single-pass tint");
 menu.ask(whj2::GameMenu::Quit);std::fill(pixels.begin(),pixels.end(),100);menu.draw(pixels);
 for(unsigned c=0;c<expected.size();++c)if(pixels[pixel+c]!=expected[c]*3/8)throw std::runtime_error("Confirmation overlay");
 std::cout<<"PASS history single composition and confirmation overlay\n";
 std::cout<<"PASS original defaults and fixed point gains; slider endpoints/controller; settings persistence; original artwork\n";return 0;
 }catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
