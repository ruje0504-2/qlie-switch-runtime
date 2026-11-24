// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene_transition.hpp"
#include "scene_images.hpp"
#include "state_io.hpp"
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
 auto rule=whj2::image(assets.read("fade/線形_縦.png"));size_t low=0,high=0;for(size_t i=0;i<rule.pixels.size();i+=4){if(rule.pixels[i]<rule.pixels[low])low=i;if(rule.pixels[i]>rule.pixels[high])high=i;}
 auto canvasIndex=[&](size_t i){auto x=(i/4)%rule.width,y=(i/4)/rule.width;return (y*720/rule.height*1280+x*1280/rule.width)*4;};auto lo=canvasIndex(low),hi=canvasIndex(high);
 qlie::Bytes dark(before.size(),0),light(before.size(),255);for(size_t i=3;i<dark.size();i+=4)dark[i]=255;
 for(const std::string mode:{"$4","$84"}){
  fade.command("^sentence,fade:rule:1000:線形_縦:"+mode,dark);fade.present();current=light;fade.blend(current);if(current!=dark)throw std::runtime_error("Rule start");
  fade.update(500);current=light;fade.blend(current);if(mode=="$4"?(current[lo]!=255||current[hi]!=0):(current[lo]!=0||current[hi]!=255))throw std::runtime_error("Rule direction/end regions");
  auto midpoint=current;whj2::SceneTransition restoredRule(assets);restoredRule.restore(fade.save());current=light;restoredRule.blend(current);if(current!=midpoint)throw std::runtime_error("Rule restore phase");
  fade.update(250);restoredRule.update(250);current=light;fade.blend(current);auto copy=light;restoredRule.blend(copy);if(current!=copy)throw std::runtime_error("Rule restored continuation");
  fade.update(250);current=light;fade.blend(current);if(current!=light)throw std::runtime_error("Rule end");
 }
 whj2::StateWriter legacy;legacy.str("WHJ2-transition-1");legacy.n(0);legacy.n(0);legacy.n(0);legacy.n(0);legacy.n(0);legacy.n(0);resumed.restore(legacy.bytes);
 std::cout<<"PASS original rule image normal/inverted direction, endpoints, save and continuation; transition1 compatibility\n";
 std::cout<<"PASS original-scene overlap start/midpoint/end, template durations, cut, save and continuation\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
