// SPDX-License-Identifier: GPL-2.0-or-later
#include "message_text.hpp"
#include "scene_images.hpp"
#include "render_queue.hpp"
extern "C" {
#include "font.h"
}
#include <iostream>
#include <algorithm>
int main(int argc,char**argv){KFont*font=nullptr;try{if(argc!=2)return 2;qlie::Assets assets(argv[1]);whj2::MessageText text(assets);font=kfont_open(nullptr,0);if(!font)throw std::runtime_error("Font open");
 whj2::SceneImages scene(assets);scene.command("^message,$window01");auto style=scene.messageStyle();if(style.width!=920||style.height!=300||style.fontWidth!=26||style.fontHeight!=26)throw std::runtime_error("Original message template style");
 text.start("文字テストABC");if(text.visible()!=0)throw std::runtime_error("Initial reveal");text.update(10);if(text.visible()!=1)throw std::runtime_error("First UTF8 glyph boundary");auto state=text.save();whj2::MessageText restored(assets);restored.restore(state);if(restored.render(font,style).pixels!=text.render(font,style).pixels)throw std::runtime_error("Message restore pixels");
 text.update(20);restored.update(20);if(restored.visible()!=text.visible()||restored.render(font,style).pixels!=text.render(font,style).pixels)throw std::runtime_error("Message continuation");
 if(!text.reveal()||!text.finished()||text.reveal())throw std::runtime_error("Reveal must consume only unfinished text");
 auto surface=text.render(font,style);auto blank=surface;std::fill(blank.pixels.begin(),blank.pixels.end(),0);scene.setMessageImage(blank,true);scene.update(250);
 qlie::Bytes frame(1280*720*4);scene.drawMessages(frame);if(std::none_of(frame.begin(),frame.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Empty original message frame");
 auto expected=frame;whj2::RenderQueue queue;queue.image(surface,whj2::imagePlacement(200,600,1,1,0,0,0,surface.width,surface.height),1100,255,0,0xffffff);queue.draw(expected,1280,720);
 scene.setMessageImage(surface);qlie::Bytes actual(frame.size());scene.drawMessages(actual);if(actual!=expected)throw std::runtime_error("Original message slot1 placement");
 auto saved=scene.save();whj2::SceneImages reloaded(assets);reloaded.restore(saved);reloaded.setMessageImage(surface);qlie::Bytes loaded(frame.size());reloaded.drawMessages(loaded);if(loaded!=actual)throw std::runtime_error("Message frame restoration");
 scene.command("^message,show:false");scene.setMessageImage(surface);scene.update(250);actual.assign(actual.size(),0);scene.drawMessages(actual);if(std::any_of(actual.begin(),actual.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Text refresh unhid window");
 text.start("[spd,0]A[spd]B");if(text.visible()!=1)throw std::runtime_error("Immediate text speed tag");text.update(9);if(text.visible()!=1)throw std::runtime_error("Default speed restored too early");auto taggedState=text.save();restored.restore(taggedState);restored.update(1);if(restored.visible()!=2)throw std::runtime_error("Tagged speed restore");
 text.start("[spd,0][pc,中央][spd]");if(text.visible()!=2||!text.finished())throw std::runtime_error("Center tag displayed literally");auto centered=text.render(font,style);restored.start("中央");restored.reveal();auto left=restored.render(font,style);auto reference=left;std::fill(reference.pixels.begin(),reference.pixels.end(),0);unsigned offset=(style.width-2*style.fontWidth)/2;
 for(unsigned y=0;y<style.height;y++)for(unsigned x=0;x+offset<style.width;x++)for(unsigned c=0;c<4;c++)reference.pixels[(size_t(y)*style.width+x+offset)*4+c]=left.pixels[(size_t(y)*style.width+x)*4+c];
 if(centered.pixels!=reference.pixels)throw std::runtime_error("Native centered origin formula");
 if(whj2::messageDisplayName("【人物＠呼び名】")!="【呼び名】"||whj2::messageDisplayName("【人物】")!="【人物】"||!whj2::messageDisplayName("").empty())throw std::runtime_error("Display name alias");
 whj2::SceneImages animated(assets);animated.command("^message,$window01");animated.setMessageImage(blank,true);qlie::Bytes opening(frame.size());animated.drawMessages(opening);if(std::any_of(opening.begin(),opening.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Enable must begin transparent");
 animated.update(125);opening.assign(opening.size(),0);animated.drawMessages(opening);if(opening==frame||std::none_of(opening.begin(),opening.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Enable midpoint did not animate");
 auto middle=animated.save();whj2::SceneImages continuation(assets);continuation.restore(middle);continuation.setMessageImage(blank);qlie::Bytes copied(frame.size());continuation.drawMessages(copied);if(copied!=opening)throw std::runtime_error("Mid-enable restore");
 animated.update(125);opening.assign(opening.size(),0);animated.drawMessages(opening);if(opening!=frame)throw std::runtime_error("Enable endpoint");
 animated.command("^message,show:false");animated.update(125);middle=animated.save();continuation.restore(middle);continuation.setMessageImage(blank);animated.update(125);continuation.update(125);opening.assign(opening.size(),0);copied.assign(copied.size(),0);animated.drawMessages(opening);continuation.drawMessages(copied);
 if(animated.messageVisible()||continuation.messageVisible()||opening!=copied||std::any_of(opening.begin(),opening.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Disable endpoint and restore");
 std::cout<<"PASS original message template/frame/text slot, UTF8 reveal, click completion and saved continuation\n";kfont_close(font);return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";if(font)kfont_close(font);return 1;}}
