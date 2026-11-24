// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene_transition.hpp"
#include "scene_images.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1]);whj2::SceneImages scene(assets);whj2::SceneTransition fade(assets);
 qlie::Bytes before(1280*720*4),after(before.size());scene.command("^bg01,file:cut/c_青空1");scene.draw(before);scene.command("^bg01,file:bg/color_白");scene.draw(after);
 if(before==after)throw std::runtime_error("Distinct original scenes required");
 if(!fade.command("^sentence,$overlap_long",before))throw std::runtime_error("Template command not consumed");
 fade.present();auto first=after;fade.blend(first);if(first!=before)throw std::runtime_error("Overlap must start with old scene");
 fade.update(750);auto halfway=after;fade.blend(halfway);
 for(size_t i=0;i<halfway.size();i++)if(halfway[i]!=(unsigned(before[i])+after[i])/2)throw std::runtime_error("Original scene midpoint");
 auto state=fade.save();whj2::SceneTransition resumed(assets);resumed.restore(state);auto restored=after;resumed.blend(restored);if(restored!=halfway)throw std::runtime_error("Saved overlap differs");
 fade.update(250);resumed.update(250);auto current=after;restored=after;fade.blend(current);resumed.blend(restored);if(current!=restored)throw std::runtime_error("Overlap continuation differs");
 resumed.update(500);restored=after;resumed.blend(restored);if(restored!=after||resumed.save().size()>100)throw std::runtime_error("Overlap completion retained frame");
 fade.command("^sentence,$cut",before);current=after;fade.blend(current);if(current!=after)throw std::runtime_error("Cut kept old frame");
 fade.command("^sentence,$overlap",before);fade.present();fade.update(500);current=after;fade.blend(current);if(current!=after)throw std::runtime_error("Default 500ms template duration");
 fade.command("^sentence,fade:catch",before);auto caught=fade.save();fade.reset();fade.restore(caught);fade.command("^sentence,$overlap",after);fade.present();current=after;fade.blend(current);if(current!=before)throw std::runtime_error("Explicit captured frame overwritten");
 fade.update(250);current=after;fade.blend(current);auto interrupted=current;fade.command("^sentence,$overlap",interrupted);fade.present();current=after;fade.blend(current);if(current!=interrupted)throw std::runtime_error("Interrupted transition popped");
 std::cout<<"PASS original-scene overlap start/midpoint/end, template durations, cut, save and continuation\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
