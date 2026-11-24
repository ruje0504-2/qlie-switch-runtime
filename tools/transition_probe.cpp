// SPDX-License-Identifier: GPL-2.0-or-later
#include "whj2/profile.hpp"
#include "scene_transition.hpp"
#include "scene_images.hpp"
#include "state_io.hpp"
#include "render_queue.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1],whj2::profile::archives);whj2::SceneImages scene(assets);whj2::SceneTransition fade(assets);
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
 whj2::StateWriter v2;v2.str("WHJ2-transition-2");v2.n(500000);v2.n(1000000);v2.n(0);v2.n(1);v2.n(0);
 v2.str("fade/線形_縦.png");v2.n(4);v2.n(dark.size());v2.bytes.insert(v2.bytes.end(),dark.begin(),dark.end());
 resumed.restore(v2.bytes);current=light;resumed.blend(current);if(current[lo]!=255||current[hi]!=0)throw std::runtime_error("Legacy rule rendering");
 std::cout<<"PASS original rule image normal/inverted direction, endpoints, save and continuation; transition1/2 compatibility\n";
 whj2::SentenceWait wait;
 fade.command("^sentence,autowait:true,fade:mosaic:400",before,&wait);
 wait.page();if(!wait.prewaiting()||wait.tick(399)!=whj2::SentenceWait::None||!wait.prewaiting())throw std::runtime_error("Mosaic auto-wait");
 restored=after;fade.blend(restored);if(restored!=after)throw std::runtime_error("Mosaic activated before present");
 fade.present();current=after;fade.blend(current);if(current!=before||!fade.needsCanvas())throw std::runtime_error("Mosaic start");
 fade.update(200);current=after;fade.blend(current);
 if(current==before||current==after||current[0]!=current[4]||current[0]!=current[1280*4])throw std::runtime_error("Mosaic midpoint blocks");
 resumed.restore(fade.save());restored=after;resumed.blend(restored);if(restored!=current)throw std::runtime_error("Mosaic save midpoint");
 resumed.update(200);restored=after;resumed.blend(restored);
 if(restored!=after||resumed.needsCanvas()||resumed.save().size()>100)throw std::runtime_error("Mosaic end");
 std::cout<<"PASS mosaic start/midpoint/end, block sampling, auto-wait and save/restore\n";
 struct MosaicPhase { unsigned step, block; uint8_t opacity; };
 const MosaicPhase phases[]={{14,1,255},{15,4,255},{96,49,255},{109,64,255},
  {127,81,127},{128,81,119},{142,64,7},{143,64,0},{240,4,0},{241,1,0}};
 constexpr unsigned width=1280,height=720;
 auto oldPattern=before,newPattern=after;
 for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
  size_t at=(size_t(y)*width+x)*4;
  oldPattern[at]=uint8_t(x);oldPattern[at+1]=uint8_t(y);oldPattern[at+2]=uint8_t(x+3*y);
  newPattern[at]=uint8_t(255-x);newPattern[at+1]=uint8_t(255-y);newPattern[at+2]=uint8_t(5*x+y);
  oldPattern[at+3]=newPattern[at+3]=255;
 }
 fade.command("^sentence,fade:mosaic:255",oldPattern);fade.present();
 unsigned lastStep=0;
 for(auto phase:phases){
  fade.update(phase.step-lastStep);lastStep=phase.step;
  current=newPattern;fade.blend(current);
  for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
   auto sampleX=std::min(width-1,x-x%phase.block+phase.block/2);
   auto sampleY=std::min(height-1,y-y%phase.block+phase.block/2);
   size_t sample=(size_t(sampleY)*width+sampleX)*4,at=(size_t(y)*width+x)*4;
   for(unsigned c=0;c<4;c++)if(current[at+c]!=whj2::blendChannel(0,oldPattern[sample+c],newPattern[sample+c],phase.opacity))
    throw std::runtime_error("Native mosaic step "+std::to_string(phase.step)+" at "+std::to_string(x)+","+std::to_string(y));
  }
 }
 std::cout<<"PASS native mosaic sizes, phase opacity and fixed-center edge sampling over ten full frames\n";
 for(const std::string asset:{"misc/fade_in.b","misc/fade_out.b"}){
  fade.command("^sentence,fade:anime:250:"+asset,before);
  auto staged=fade.save();resumed.restore(staged);resumed.present();
  fade.present();bool captures=asset!="misc/fade_out.b";
  if(fade.needsTarget()!=captures||resumed.needsTarget()!=captures)throw std::runtime_error("ABMP CaptureFadein "+asset);
  fade.captureTarget(after);resumed.captureTarget(after);
  whj2::SceneImages original(assets);
  original.command("^ui_expected,file:"+asset+",x:0,y:0");
  original.uiSlot("ui_expected",0,whj2::Image{1280,720,before});
  if(captures)original.uiSlot("ui_expected",1,whj2::Image{1280,720,after});
  auto authored=after;original.draw(authored);
  current=after;fade.blend(current);restored=after;resumed.blend(restored);
  if(current!=restored||current!=authored||current==after)throw std::runtime_error("Animation first keyframe "+asset);
  fade.update(90);current=after;fade.blend(current);
  original.update(360);auto middle=after;original.draw(middle);
  if(current==authored||current==after||current!=middle)throw std::runtime_error("Animation middle keyframe "+asset);
  resumed.restore(fade.save());restored=after;resumed.blend(restored);
  if(restored!=current)throw std::runtime_error("Animation captured frame restore "+asset);
  fade.update(80);resumed.update(80);current=after;restored=after;fade.blend(current);resumed.blend(restored);
  if(current!=restored)throw std::runtime_error("Animation continued at wrong phase "+asset);
  fade.update(80);current=after;fade.blend(current);
  if(current!=after||fade.needsTarget()||fade.save().size()>100)throw std::runtime_error("Animation completion "+asset);
  std::cout<<"PASS "<<asset<<" two endpoints, middle frames and staged/active save restore\n";
 }
 std::cout<<"PASS original-scene overlap start/midpoint/end, template durations, cut, save and continuation\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
