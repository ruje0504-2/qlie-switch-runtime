// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene_images.hpp"
#include "audio.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc,char**argv){try{if(argc!=2)return 2;
 if(SDL_Init(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
 {qlie::Assets assets(argv[1]);whj2::SceneImages images(assets);whj2::Audio audio;
 images.command("^bg01,file:cut/踏切点灯");auto events=images.takeSoundEvents();
 if(events.size()!=1||events[0].channel!=4||!events[0].loop||events[0].gain!=1)throw std::runtime_error("Original crossing sound event fields");
 if(!images.takeSoundEvents().empty())throw std::runtime_error("Duplicate embedded start");
 audio.play(events[0].channel,events[0].bytes,events[0].loop,events[0].gain,0,events[0].source);
 SDL_Delay(150);auto before=audio.stats();if(!before.nonzero)throw std::runtime_error("No embedded PCM output");
 images.update(150);images.takeSoundEvents();auto imageState=images.save();auto audioState=audio.save();
 whj2::SceneImages restored(assets);restored.restore(imageState);if(!restored.takeSoundEvents().empty())throw std::runtime_error("Restored event retriggered");
 audio.stopAll();audio.restore(audioState,assets);SDL_Delay(150);auto after=audio.stats();
 if(after.nonzero<=before.nonzero||!audio.playing(4))throw std::runtime_error("Embedded audio resume failed");
 whj2::SceneImages delayed(assets);delayed.command("^effect,file:cut/世界崩壊1");
 if(!delayed.takeSoundEvents().empty())throw std::runtime_error("Delayed event fired at start");
 delayed.update(1089);if(!delayed.takeSoundEvents().empty())throw std::runtime_error("Delayed event fired early");
 delayed.update(1);auto event=delayed.takeSoundEvents();if(event.size()!=1||event[0].loop||event[0].gain!=1)throw std::runtime_error("1090ms sound boundary");
 if(!delayed.takeSoundEvents().empty())throw std::runtime_error("Repeated delayed event");
 std::cout<<"PASS embedded start/default volume/loop, exact delayed boundary, scene cursor restore, PCM source restore\n";
 std::cout<<"CoreAudio callback frames="<<after.frames<<" nonzero="<<after.nonzero<<" resumed_nonzero="<<after.nonzero-before.nonzero<<"\n";
 }SDL_Quit();return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";SDL_Quit();return 1;}}
