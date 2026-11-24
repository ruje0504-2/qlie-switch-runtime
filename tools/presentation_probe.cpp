#include "profiles/whj2.hpp"
#include "story.hpp"
#include "scene_images.hpp"
#include "animation.hpp"
#include "render_queue.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>
int main(int argc,char**argv){try{if(argc<2)return 2;qlie::Assets a(argv[1],whj2::profile::archives);
if(argc==3&&std::string(argv[2])=="--face-render"){
 whj2::SceneImages scene(a);const std::string file="めぐみ/めぐみ_小_制服_1_05通常05";
 scene.command("^face,$base,file:"+file);qlie::Bytes actual(1280*720*4),expected(actual.size()),background(actual.size());scene.draw(actual);scene.drawScene(background);
 if(std::any_of(background.begin(),background.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Face leaked into background capture");
 auto portrait=whj2::image(a.read("face/"+file+".png"));whj2::RenderQueue queue;queue.image(portrait,whj2::imagePlacement(100,720,1,1,0,.5,1,portrait.width,portrait.height),1110,255,0,0xffffff);queue.draw(expected,1280,720);
 if(actual!=expected||std::none_of(actual.begin(),actual.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Original face template placement");
 auto state=scene.save();whj2::SceneImages loaded(a);loaded.restore(state);qlie::Bytes restored(actual.size());loaded.drawFaces(restored);if(restored!=actual)throw std::runtime_error("Face restore");
 scene.beginPage();actual.assign(actual.size(),0);scene.drawFaces(actual);if(actual!=background)throw std::runtime_error("Once face remained on next page");
 scene.command("^face,file:"+file+",disp:keep");scene.beginPage();scene.drawFaces(actual);if(actual!=expected)throw std::runtime_error("Keep face lost on next page");
 scene.command("^face,show:false");actual.assign(actual.size(),0);scene.drawFaces(actual);if(actual!=background)throw std::runtime_error("Hidden face visible");
 scene.command("^face,clear:def");scene.drawFaces(actual);if(actual!=background)throw std::runtime_error("Face clear default");
 scene.command("^chara01,file:"+file);scene.command("^face,$base");scene.beginPage();scene.selectFace("【めぐみ＠別名】","");actual.assign(actual.size(),0);scene.drawFaces(actual);if(actual!=expected)throw std::runtime_error("Automatic speaker-to-portrait mapping");
 scene.command("^chara01,id:別名");scene.beginPage();scene.selectFace("【別名】","");actual.assign(actual.size(),0);scene.drawFaces(actual);if(actual!=expected)throw std::runtime_error("Character explicit ID mapping");
 scene.beginPage();scene.selectFace("","");actual.assign(actual.size(),0);scene.drawFaces(actual);if(actual!=background)throw std::runtime_error("Narration kept automatic once portrait");
 std::cout<<"PASS original face placement, overlay isolation, once/keep/show/clear and saved state\n";return 0;
}
if(argc==3&&std::string(argv[2])=="--image-replacement"){
 auto draw=[](whj2::SceneImages&scene){qlie::Bytes out(1280*720*4);scene.draw(out);return out;};
 const std::string base="^bg01,file:cut/eyecatch01,x:640,y:360,align:center:center";
 whj2::SceneImages original(a),changed(a),reference(a);original.command(base);original.update(700);auto plain=draw(original);
 changed.command(base+",rep0:cut/eyecatch01_蓮花");changed.update(700);auto first=draw(changed);if(first==plain)throw std::runtime_error("Replacement left original pixels");
 reference.command("^ui_reference,file:image/cut/eyecatch01.b,x:640,y:360,align:center:center");reference.uiSlot("ui_reference",0,"image/cut/eyecatch01_蓮花.png");reference.update(700);if(draw(reference)!=first)throw std::runtime_error("Replacement did not target original image slot zero");
 auto saved=changed.save();whj2::SceneImages loaded(a);loaded.restore(saved);if(draw(loaded)!=first)throw std::runtime_error("Replacement lost on restore");
 changed.command("^bg01,rep0:cut/eyecatch01_雫");auto second=draw(changed);if(second==first)throw std::runtime_error("Replacement update did not change image");
 if(draw(loaded)!=first)throw std::runtime_error("Replacement leaked between instances");
 changed.command(base);changed.update(700);if(draw(changed)!=plain)throw std::runtime_error("New base file kept stale replacement");
 changed.command("^bg01,rep0:cut/eyecatch01_蓮花,rep1:cut/eyecatch01_雫");if(draw(changed)!=second)throw std::runtime_error("Original rep order/slot mapping");
 changed.command("^bg01,rep0:none");auto cleared=draw(changed);if(cleared==second)throw std::runtime_error("Replacement none did not clear slot");
 auto blank=changed.save();loaded.restore(blank);if(draw(loaded)!=cleared)throw std::runtime_error("Empty replacement lost on restore");
 whj2::SceneImages flash(a);flash.command("^bg01,file:effect/フラッシュH,rep0:ev/w＠08_04_1_014");flash.update(100);draw(flash);auto shown=flash.takeShownFiles();if(!shown.count("image/ev/w＠08_04_1_014.png"))throw std::runtime_error("Replacement CG not recorded as displayed");
 std::cout<<"PASS original replacement pixels / slot order / restore / instance isolation / file reset / none / CG tracking\n";return 0;
}
if(argc==3&&std::string(argv[2])=="--named-filter"){
 for(const auto&item:std::vector<std::pair<std::string,std::string>>{{"pass","0"},{"point","1"},{"linear","2"}}){
  whj2::SceneImages named(a),numeric(a);auto base="^bg01,$base_bg,file:cut/eyecatch01,scalex:37,scaley:37,texfilter:";
  named.command(base+item.first);numeric.command(base+item.second);named.update(700);numeric.update(700);
  qlie::Bytes x(1280*720*4),y(x.size());named.draw(x);numeric.draw(y);if(x!=y)throw std::runtime_error("Named filter differs from numeric "+item.first);
  std::cout<<"PASS named texture filter "<<item.first<<" equals "<<item.second<<"\n";
 }return 0;
}
if(argc==3&&std::string(argv[2])=="--dynamic-instances"){
 unsigned failures=0;
 for(const std::string resource:{"cut/eyecatch01","cut/c_万華鏡"}){
  auto command=[&](const std::string&layer,int x){return "^"+layer+",file:"+resource+",x:"+std::to_string(x)+",y:360,scalex:20,scaley:20";};
  whj2::SceneImages left(a),right(a),different(a);left.command(command("bg01",160));left.update(700);right.command(command("bg01",1120));right.update(2100);
  different.command(command("bg01",160));different.update(2100);qlie::Bytes early(1280*720*4),late(early.size());left.draw(early);different.draw(late);if(early==late)throw std::runtime_error("Dynamic test times did not produce distinct frames "+resource);
  auto bounds=[](const qlie::Bytes&im){int lo=1280,hi=-1;for(int y=0;y<720;y++)for(int x=0;x<1280;x++)if(im[(size_t(y)*1280+x)*4+3]){lo=std::min(lo,x);hi=std::max(hi,x);}return std::to_string(lo)+":"+std::to_string(hi);};
  qlie::Bytes rightOnly(early.size());right.draw(rightOnly);std::cout<<"BOUNDS "<<resource<<" left="<<bounds(early)<<" right="<<bounds(rightOnly)<<"\n";
  qlie::Bytes expected(early.size());left.draw(expected);right.draw(expected);
  for(bool reverse:{false,true}){whj2::SceneImages combined(a);combined.command(command(reverse?"bg01":"bg02",1120));combined.update(1400);combined.command(command(reverse?"bg02":"bg01",160));combined.update(700);
   qlie::Bytes actual(expected.size());combined.draw(actual);size_t mismatches=0,leftDiff=0,rightDiff=0;for(size_t i=0;i<actual.size();++i)if(actual[i]!=expected[i]){++mismatches;if((i/4)%1280<640)++leftDiff;else ++rightDiff;} std::cout<<"HALVES "<<leftDiff<<","<<rightDiff<<"\n";
   std::cout<<(mismatches?"FAIL":"PASS")<<" dynamic instances "<<resource<<" reverse="<<reverse<<" different_bytes="<<mismatches<<"\n";if(mismatches)++failures;
  }
 }if(failures)throw std::runtime_error("Dynamic image lifetime aliasing");return 0;
}
if(argc==3&&std::string(argv[2])=="--chara-base"){
 auto frame=[](whj2::SceneImages&images){qlie::Bytes result(1280*720*4);images.draw(result);return result;};
 const std::string portrait="考える01:蓮華/蓮華_中_着物_2_";
 whj2::SceneImages empty(a);empty.command("^chara01,base:motion/motion01");auto blank=frame(empty);if(std::any_of(blank.begin(),blank.end(),[](uint8_t v){return v!=0;}))throw std::runtime_error("Unassigned character slot showed placeholder");
 whj2::SceneImages animated(a),plain(a);animated.command("^chara01,base:motion/motion01,file:"+portrait);plain.command("^chara01,file:"+portrait);
 if(frame(animated)!=frame(plain))throw std::runtime_error("Default base portrait placement");
 animated.update(5000);if(frame(animated)!=frame(plain))throw std::runtime_error("Base direct-key loop disappeared");
 animated.command("^chara01,motion:頷く");animated.update(100);plain.command("^chara01,y:730");
 if(frame(animated)!=frame(plain))throw std::runtime_error("Original nod key displacement");
 auto state=animated.save();whj2::SceneImages restored(a);restored.restore(state);if(frame(restored)!=frame(animated))throw std::runtime_error("Replacement portrait restore");
 animated.update(1800);restored.update(1800);plain.command("^chara01,y:720");if(frame(restored)!=frame(animated)||frame(animated)!=frame(plain))throw std::runtime_error("Nod looping hold and restored continuation");
 animated.command("^chara01,x:500");plain.command("^chara01,x:500");if(frame(animated)!=frame(plain))throw std::runtime_error("Character next-command default reset");
 animated.command("^chara01,motion:1");animated.update(100);plain.command("^chara01,y:730");if(frame(animated)!=frame(plain))throw std::runtime_error("Numeric motion selection");
 animated.command("^chara01,x:300,y:720");plain.command("^chara01,x:300,y:720");
 animated.command("^chara02,base:motion/motion01,file:通常01:蓮華/蓮華_中_着物_2_,x:950");plain.command("^chara02,file:通常01:蓮華/蓮華_中_着物_2_,x:950");
 if(frame(animated)!=frame(plain))throw std::runtime_error("Shared base leaked portrait replacements");
 auto pairState=animated.save();restored.restore(pairState);if(frame(restored)!=frame(plain))throw std::runtime_error("Two replacement portraits restore");
 std::cout<<"PASS original character base slot/default, nod/hold, save continuation and command reset\n";return 0;
}
if(argc==3&&std::string(argv[2])=="--tween-render"){
 auto frame=[](whj2::SceneImages&images){qlie::Bytes result(1280*720*4);images.draw(result);return result;};
 whj2::SceneImages moving(a),expected(a);
 moving.command("^bg01,file:cut/c_青空1");expected.command("^bg01,file:cut/c_青空1");
 moving.command("^bg01,x:740,time:1000");moving.update(500);expected.command("^bg01,x:690,time:0");
 if(frame(moving)!=frame(expected))throw std::runtime_error("Linear camera midpoint");
 auto state=moving.save();whj2::SceneImages restored(a);restored.restore(state);
 moving.update(250);restored.update(250);expected.command("^bg01,x:715,time:0");
 if(frame(moving)!=frame(expected)||frame(restored)!=frame(expected))throw std::runtime_error("Mid-tween restored continuation");
 moving.command("^bg01,x:815,time:1000");moving.update(500);expected.command("^bg01,x:765,time:0");
 if(frame(moving)!=frame(expected))throw std::runtime_error("Retarget must start from current position");
 moving.command("^bg01,x:640,ax:-30,time:0");expected.command("^bg01,x:610,time:0");
 if(frame(moving)!=frame(expected))throw std::runtime_error("Axial position offset");
 moving.command("^bg01,ax:0,time:1000,mulcolor:$000000");moving.update(500);
 expected.command("^bg01,x:625,mulcolor:$7F7F7F,time:0");
 if(frame(moving)!=frame(expected))throw std::runtime_error("Color channels and position interpolate independently");
 whj2::SceneImages slow(a),normal(a);slow.command("^bg01,file:cut/c_冒頭イメージ,motion:c01,speed:50");normal.command("^bg01,file:cut/c_冒頭イメージ,motion:c01");slow.update(2000);normal.update(1000);
 if(frame(slow)!=frame(normal))throw std::runtime_error("Half-speed animation clock");
 std::cout<<"PASS original bitmap camera midpoint, retarget, axial offset, color ramp, save continuation and half-speed particles\n";return 0;
}
if(argc==3&&std::string(argv[2])=="--nested-render"){
 whj2::SceneImages im(a);im.command("^bg01,file:cut/eyecatch01");im.update(1500);
 qlie::Bytes frame(1280*720*4),restored(frame.size());im.draw(frame);
 auto state=im.save();whj2::SceneImages second(a);second.restore(state);second.draw(restored);
 if(frame!=restored)throw std::runtime_error("Nested clock restore differs");
 im.update(750);second.update(750);frame.assign(frame.size(),0);restored.assign(restored.size(),0);im.draw(frame);second.draw(restored);
 if(frame!=restored)throw std::runtime_error("Nested clock continuation differs");
 size_t count=0;for(auto v:frame)if(v)count++;
 if(!count)throw std::runtime_error("Nested scene empty");
 std::cout<<"PASS original eyecatch nested clock drawing/save/continuation pixels="<<1280*720<<" nonzero_bytes="<<count<<"\n";return 0;
}
if(argc==3&&std::string(argv[2])=="--particle-render"){
 whj2::SceneImages im(a);auto emptyState=im.save();im.command("^bg01,file:cut/c_冒頭イメージ,motion:c01");im.update(1234);
 qlie::Bytes first(1280*720*4),again(first.size()),restored(first.size());im.draw(first);im.draw(again);
 if(first!=again)throw std::runtime_error("Particle frame changed without clock advance");
 auto state=im.save();whj2::SceneImages second(a);second.restore(state);second.draw(restored);
 if(first!=restored)throw std::runtime_error("Particle identity changed across restore");
 im.update(137);second.update(137);first.assign(first.size(),0);restored.assign(restored.size(),0);im.draw(first);second.draw(restored);
 if(first!=restored)throw std::runtime_error("Particle restored continuation diverged");
 im.restore(emptyState);whj2::SceneImages fresh(a);fresh.restore(emptyState);
 for(auto scene:{&im,&fresh}){scene->command("^effect,file:effect/effect01,motion:縦衝撃");scene->command("^bg01,file:cut/c_冒頭イメージ,motion:c01");scene->update(1234);}
 first.assign(first.size(),0);restored.assign(restored.size(),0);im.draw(first);fresh.draw(restored);
 if(first!=restored)throw std::runtime_error("Particle cache identity diverged after old save and changed load order");
 size_t nonzero=0;for(auto byte:first)if(byte)nonzero++;
 if(!nonzero)throw std::runtime_error("Empty opening image");
 std::cout<<"PASS original particle scene repeated frame and cross-instance save/continuation pixels="<<1280*720<<" nonzero_bytes="<<nonzero<<"\n";return 0;
}
if(argc==3&&std::string(argv[2])=="--effect-render"){
 whj2::SceneImages im(a);
 im.command("^bg01,file:cut/c_青空1");
 qlie::Bytes base(1280*720*4),out(base.size()),expected(base.size());im.draw(base);
 im.command("^effect,file:effect/effect01,motion:縦衝撃,x:0,y:0,pri:-10");
 auto verify=[&](int offset){
  out.assign(out.size(),0);expected.assign(expected.size(),0);im.draw(out);
  for(int y=0;y<720;y++){
   int from=y-offset;if(from<0||from>=720)continue;
   std::copy(base.begin()+from*1280*4,base.begin()+(from+1)*1280*4,expected.begin()+y*1280*4);
  }
  if(out!=expected)throw std::runtime_error("Actual background displacement pixels mismatch: "+std::to_string(offset));
  std::cout<<"PASS original effect/background offset="<<offset<<" pixels="<<1280*720<<"\n";
 };
 im.update(25);verify(10);im.update(50);verify(-10);im.update(125);verify(0);
 return 0;
}
if(argc==3&&std::string(argv[2])=="--rectangle-render"){
 whj2::SceneImages im(a);im.command("^bg01,file:cut/バレーヒット,motion:A");im.update(130);
 qlie::Bytes frame(1280*720*4);im.draw(frame);
 size_t white=0;for(size_t i=0;i<frame.size();i+=4)if(frame[i]==255&&frame[i+1]==255&&frame[i+2]==255&&frame[i+3]==255)white++;
 std::cout<<"Original volley rectangle white_pixels="<<white<<" total="<<1280*720<<"\n";
 if(white!=1280*720)throw std::runtime_error("Volley full-frame white rectangle mismatch");
 std::cout<<"PASS original rectangle rendering\n";return 0;
}
if(argc==4&&std::string(argv[2])=="--flash-render"){
 std::ifstream input(argv[3],std::ios::binary);qlie::Bytes lut((std::istreambuf_iterator<char>(input)),{});
 if(lut.size()!=131072)throw std::runtime_error("Original MMX reference table size");
 whj2::SceneImages im(a);im.command("^bg01,file:cut/c_青空1");
 qlie::Bytes base(1280*720*4),frame(base.size());im.draw(base);
 im.command("^effect,file:effect/effect01,motion:フラッシュ,x:0,y:0,pri:-10");
 unsigned alpha=256;uint32_t tint=0;
 for(auto&part:whj2::abmp(a.read("image/effect/effect01.b")))if(part.type.rfind("abdata",0)==0){
  auto definition=whj2::animation(part.type,part.data);
  for(auto&motion:definition.motions)if(motion.name=="フラッシュ")for(auto&layer:motion.layers)if(layer.flags){
   auto k=whj2::sampleAnimation(layer,50,whj2::AnimationContext{1280,720,1280,720});
   if(k.kind==1){alpha=unsigned(k.alpha);tint=k.mulColor;}
  }
 }
 if(alpha!=255)throw std::runtime_error("Flash reference table requires opaque source alpha");
 im.update(50);im.draw(frame);
 size_t mismatch=0;
 for(size_t i=0;i<base.size();i+=4){
  for(unsigned c=0;c<3;c++)if(frame[i+c]!=lut[((((tint>>(8*c))&255)*256)+base[i+c])*2])mismatch++;
  if(frame[i+3]!=lut[(((alpha)*256)+255)*2+1])mismatch++;
 }
 std::cout<<"Original flash alpha="<<alpha<<" compared_channels="<<base.size()<<" mismatches="<<mismatch<<"\n";
 if(mismatch)throw std::runtime_error("Flash pixels differ from original MMX");
 std::cout<<"PASS original flash whole-frame blend\n";return 0;
}
whj2::Story s(a,whj2::profile::story);whj2::SceneImages images(a);s.start();std::string bits=argc>2?argv[2]:"000100";unsigned decisions=0,commands=0;for(unsigned n=0;n<2000000;n++){auto e=s.next();try{if(e.type=="choice")s.choose(decisions<bits.size()?unsigned(bits[decisions++]-48):0);else if(e.type=="create"||e.type=="free")images.clear();else if(e.type=="command"&&images.command(e.value))commands++;else if(e.type=="finish"||e.type=="finished"){std::cout<<"PASS static image bindings commands="<<commands<<" (animation/property warnings remain unimplemented)\n";return 0;}}catch(const std::exception&x){throw std::runtime_error(e.file+":"+std::to_string(e.line)+" "+e.value+": "+x.what());}}throw std::runtime_error("No ending");}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
