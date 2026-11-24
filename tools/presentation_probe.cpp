#include "story.hpp"
#include "scene_images.hpp"
#include <iostream>
int main(int argc,char**argv){try{if(argc<2)return 2;qlie::Assets a(argv[1]);
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
whj2::Story s(a);whj2::SceneImages images(a);s.start();std::string bits=argc>2?argv[2]:"000100";unsigned decisions=0,commands=0;for(unsigned n=0;n<2000000;n++){auto e=s.next();try{if(e.type=="choice")s.choose(decisions<bits.size()?unsigned(bits[decisions++]-48):0);else if(e.type=="create"||e.type=="free")images.clear();else if(e.type=="command"&&images.command(e.value))commands++;else if(e.type=="finish"||e.type=="finished"){std::cout<<"PASS static image bindings commands="<<commands<<" (animation/property warnings remain unimplemented)\n";return 0;}}catch(const std::exception&x){throw std::runtime_error(e.file+":"+std::to_string(e.line)+" "+e.value+": "+x.what());}}throw std::runtime_error("No ending");}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
