#include "story.hpp"
#include "scene_images.hpp"
#include "animation.hpp"
#include <fstream>
#include <iostream>
int main(int argc,char**argv){try{if(argc<2)return 2;qlie::Assets a(argv[1]);
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
whj2::Story s(a);whj2::SceneImages images(a);s.start();std::string bits=argc>2?argv[2]:"000100";unsigned decisions=0,commands=0;for(unsigned n=0;n<2000000;n++){auto e=s.next();try{if(e.type=="choice")s.choose(decisions<bits.size()?unsigned(bits[decisions++]-48):0);else if(e.type=="create"||e.type=="free")images.clear();else if(e.type=="command"&&images.command(e.value))commands++;else if(e.type=="finish"||e.type=="finished"){std::cout<<"PASS static image bindings commands="<<commands<<" (animation/property warnings remain unimplemented)\n";return 0;}}catch(const std::exception&x){throw std::runtime_error(e.file+":"+std::to_string(e.line)+" "+e.value+": "+x.what());}}throw std::runtime_error("No ending");}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
