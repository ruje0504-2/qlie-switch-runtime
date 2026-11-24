// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene_images.hpp"
#include "story.hpp"
#include "state_io.hpp"
#include "animation.hpp"
#include <set>
extern "C" {
#include "video.h"
}
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cmath>
namespace whj2 {
namespace {
float numeric(const std::string&s){return !s.empty()&&s[0]=='$'?float(std::stoul(s.substr(1),nullptr,16)):std::stof(s);
 }
void blit(qlie::Bytes&out,unsigned width,unsigned height,const Image&im,float x,float y,float sx,float sy,float rotation,float anchorX,float anchorY,float alpha,uint32_t add,uint32_t mul,const Affine2D&post={}){
 if(!im.width||!im.height||!sx||!sy||alpha<=0)return;
 constexpr float pi=3.14159265358979323846f;
 float c=std::cos(rotation*pi/180),s=std::sin(rotation*pi/180);
 float ax=im.width*anchorX,ay=im.height*anchorY;
 float a=post.a*c*sx+post.b*s*sx,b=-post.a*s*sy+post.b*c*sy;
 float cc=post.c*c*sx+post.d*s*sx,d=-post.c*s*sy+post.d*c*sy;
 float px=post.a*x+post.b*y+post.x,py=post.c*x+post.d*y+post.y;
 float det=a*d-b*cc;if(std::abs(det)<1e-12f)return;
 float xmin=px,xmax=px,ymin=py,ymax=py;
 for(int i=0;i<4;i++){float u=((i&1)?im.width:0)-ax,v=((i&2)?im.height:0)-ay;
 float xx=px+a*u+b*v,yy=py+cc*u+d*v;
 xmin=std::min(xmin,xx);
 xmax=std::max(xmax,xx);
 ymin=std::min(ymin,yy);
 ymax=std::max(ymax,yy);
 }
 for(int yy=std::max(0,int(std::floor(ymin)));yy<std::min(int(height),int(std::ceil(ymax)));yy++)for(int xx=std::max(0,int(std::floor(xmin)));xx<std::min(int(width),int(std::ceil(xmax)));xx++){
  float dx=xx+0.5f-px,dy=yy+0.5f-py;
 int u=int(std::floor((d*dx-b*dy)/det+ax)),v=int(std::floor((-cc*dx+a*dy)/det+ay));
 if(u<0||v<0||u>=int(im.width)||v>=int(im.height))continue;
  const auto*src=im.pixels.data()+(size_t(v)*im.width+u)*4;
 auto*dst=out.data()+(size_t(yy)*width+xx)*4;
 unsigned sa=unsigned(src[3])*unsigned(std::clamp(alpha,0.f,255.f))/255;
 if(!sa)continue;
 unsigned da=dst[3],oa=sa+da*(255-sa)/255;
  for(unsigned n=0;n<3;n++){unsigned col=std::min(255u,unsigned(src[n])*((mul>>(8*n))&255)/255+((add>>(8*n))&255));
 dst[n]=uint8_t((col*sa+unsigned(dst[n])*da*(255-sa)/255)/oa);
 }dst[3]=uint8_t(oa);
 }
}
}
struct EmbeddedVideo {
 const qlie::Bytes&bytes;
 KVideo*video=nullptr;
 Image frame;
 uint64_t ticks=0;
 bool ended=false;
 explicit EmbeddedVideo(const qlie::Bytes&b):bytes(b){reset();
 }
 ~EmbeddedVideo(){if(video)kvideo_close(video);
 }
 void reset(){if(video)kvideo_close(video);
 video=kvideo_open_mode(bytes.data(),bytes.size(),KVIDEO_SOFTWARE);
 if(!video)throw std::runtime_error("Embedded video open failed");
 frame={kvideo_source_width(video),kvideo_source_height(video),{}};
 frame.pixels.resize(size_t(frame.width)*frame.height*4);
 ticks=0;
 ended=false;
 }
 const Image&at(double ms){uint64_t target=uint64_t(std::max(0.,ms)*60/1000);
 if(target+1<ticks)reset();
 while(!ended&&ticks<=target){KImage dst{0,0,frame.width,frame.height,size_t(frame.width)*4,frame.pixels.data()};
 uint8_t*pcm=nullptr;
 size_t count=0;
 int code=kvideo_step(video,&dst,&pcm,&count);
 free(pcm);
 ticks++;
 if(code<0)throw std::runtime_error(kvideo_error(video));
 ended=code==1;
 }return frame;
 }
};
struct Composite {
 struct Element {std::shared_ptr<Image>image;
 std::shared_ptr<Composite>nested;
 qlie::Bytes video;
 };
 Animation definition;
 std::vector<Element>members;
 std::string path;
 mutable std::set<std::string>warnings;
 mutable std::map<size_t,std::unique_ptr<EmbeddedVideo>>clips;
 explicit Composite(const qlie::Bytes&bytes,const std::string&name):path(name){
  bool metadata=false;
  for(auto&m:abmp(bytes)){
   if(m.type.rfind("abdata",0)==0){definition=animation(m.type,m.data);
 metadata=true;
 }
   else if(m.type.rfind("abimg",0)==0){Element e;
 if(!m.data.empty()){
    if(m.data.size()>=5&&std::equal(m.data.begin(),m.data.begin()+5,"abmp1"))e.nested=std::make_shared<Composite>(m.data,name+"#"+m.name);
    else if(m.data.size()>=4&&std::equal(m.data.begin(),m.data.begin()+4,"OggS")){e.video=std::move(m.data);
 }
    else if(m.data.size()>=12&&std::equal(m.data.begin(),m.data.begin()+12,"ARGBModel1.0"))warn("model="+m.name);
    else e.image=std::make_shared<Image>(image(m.data));
   }members.push_back(std::move(e));
 }
   else if(m.type.rfind("absnd",0)==0&&!m.data.empty())warn("embedded-audio="+m.name);
  }
  if(!metadata)throw std::runtime_error("Composite lacks animation metadata "+name);
  for(auto&motion:definition.motions)for(auto&layer:motion.layers){if(layer.mode>2)warn("layer-mode="+std::to_string(layer.mode));
 for(auto&k:layer.keys)if(k[4]!=0&&k[4]!=4&&k[4]!=15&&k[4]!=16)warn("primitive="+std::to_string(k[4]));
 }
 }
 void warn(const std::string&s)const{if(warnings.insert(s).second)std::cerr<<"PENDING-ANIMATION "<<path<<" "<<s<<"\n";
 }
 Image render(double ms,const std::string&motionName)const{
  if(definition.bounds[0]<=0||definition.bounds[1]<=0||definition.bounds[0]>4096||definition.bounds[1]>4096)throw std::runtime_error("Composite bounds "+path);
  const Motion*motion=nullptr;
 for(auto&m:definition.motions)if(m.name==motionName)motion=&m;
 if(!motion&&(motionName.empty()||motionName=="def"||motionName=="0")&&!definition.motions.empty())motion=&definition.motions.front();
 if(!motion)throw std::runtime_error("Missing motion "+motionName+" in "+path);
  Image frame{unsigned(definition.bounds[0]),unsigned(definition.bounds[1]),qlie::Bytes(size_t(definition.bounds[0])*definition.bounds[1]*4)};
  auto samples=sampleMotion(*motion,ms,frame.width,frame.height);
  for(size_t index=0;index<samples.size();index++){
 auto&k=samples[index];
 for(auto&w:k.unsupported)warn(w);
 if(!motion->layers[index].flags||k.kind!=4)continue;
 if(k.resource>=members.size())throw std::runtime_error("Composite image index "+path);
   auto&e=members[k.resource];
 Image nested;
 const Image*source=e.image.get();
   if(e.nested){warn("nested-motion origin alignment unverified");
 nested=e.nested->render(ms,"");
 source=&nested;
 }
   else if(!e.video.empty()){
    auto&clip=clips[index];
 if(!clip||&clip->bytes!=&e.video){clip=std::make_unique<EmbeddedVideo>(e.video);
 if(kvideo_has_audio(clip->video))warn("video-audio requires mixer binding");
 }
    warn("video-member origin alignment unverified");
 source=&clip->at(ms);
   }
   if(source)blit(frame.pixels,frame.width,frame.height,*source,k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,k.alpha,k.addColor,k.mulColor,k.screen);
  }
  return frame;
 }
};
SceneImages::SceneImages(qlie::Assets&a):assets(a){for(auto type:{"image","chara"}){std::istringstream in(qlie::cp932(a.read(std::string("avg/imoscripter_template_")+type+".txt")));
 std::string s;
 while(std::getline(in,s)){auto p=s.find(61);
 if(p!=s.npos)templates[type][trim(s.substr(0,p))]=trim(s.substr(p+1));
 }}}
std::string SceneImages::expand(std::string s,const std::string&type){auto&map=templates[type];
 std::vector<std::pair<std::string,std::string>>values(map.begin(),map.end());
 std::sort(values.begin(),values.end(),[](auto&a,auto&b){return a.first.size()>b.first.size();});
 for(unsigned pass=0;pass<12;pass++){bool changed=false;
 for(auto&v:values){size_t p=0;
 while((p=s.find(v.first,p))!=s.npos){s.replace(p,v.first.size(),v.second);
 p+=v.second.size();
 changed=true;
 }}if(!changed)return s;
 }throw std::runtime_error("Image template recursion");
 }
void SceneImages::clear(){layers.clear();
 cache.clear();
 composites.clear();
 }
void SceneImages::update(double ms){for(auto&l:layers)l.second.milliseconds+=ms;
 }
bool SceneImages::command(const std::string&line){auto p=line.find(44);
 auto cmd=line.substr(1,p-1);
 bool chara=cmd.rfind("chara",0)==0;
 if(!chara&&cmd.rfind("bg",0)!=0&&cmd!="effect"&&cmd!="env")return false;
 auto&l=layers[cmd];
 if(l.params.empty())l.params={{"x","640"},{"y",chara?"720":"360"},{"scalex","100"},{"scaley","100"},{"alpha","255"},{"pri",chara?"500":"0"},{"show","true"},{"align",chara?"center:bottom":"center:center"}};
 auto args=split(expand(p==line.npos?"":line.substr(p+1),chara?"chara":"image"),44);
 bool file=false;
 for(auto&s:args){if(s.empty())continue;
 auto c=s.find(58);
 if(c==s.npos)throw std::runtime_error("Unknown image template "+s);
 auto key=s.substr(0,c),v=s.substr(c+1);
 if(key=="clear"){l.path.clear();
 l.pixels.reset();
 l.composite.reset();
 l.params["show"]="false";
 continue;
 }l.params[key]=v;
 if(key=="file")file=true;
 if(key=="motion")l.milliseconds=0;
 if(key!="file"&&key!="x"&&key!="y"&&key!="scalex"&&key!="scaley"&&key!="alpha"&&key!="show"&&key!="pri"&&key!="align"&&key!="texfilter"&&key!="rotz"&&key!="mulcolor"&&key!="addcolor")std::cerr<<"PENDING-IMAGE-PROPERTY "<<cmd<<" "<<key<<"="<<v<<"\n";
 }
 if(file){std::string f=l.params["file"];
 if(f=="none"||f=="none:none"){l.path.clear();
 l.pixels.reset();
 l.composite.reset();
 return true;
 }if(chara){auto pair=split(f,58);
 if(pair.size()>1)l.prefix=pair[1];
 f=pair[0].find(47)!=std::string::npos?pair[0]:l.prefix+pair[0];
 }std::string path=(chara?"chara/":"image/")+f;
 if(!assets.exists(path)){if(assets.exists(path+".png"))path+=".png";
 else if(assets.exists(path+".b"))path+=".b";
 else throw std::runtime_error("Missing image "+path);
 }l.path=path;
 if(path.size()>2&&path.substr(path.size()-2)==".b"){l.pixels.reset();
 auto it=composites.find(path);
 if(it==composites.end())it=composites.emplace(path,std::make_shared<Composite>(assets.read(path),path)).first;
 l.composite=it->second;
 l.milliseconds=0;
 return true;
 }auto it=cache.find(path);
 if(it==cache.end())it=cache.emplace(path,std::make_shared<Image>(image(assets.read(path)))).first;
 l.pixels=it->second;
 l.composite.reset();
 l.milliseconds=0;
 std::cerr<<"IMAGE loaded "<<path<<" "<<l.pixels->width<<"x"<<l.pixels->height<<"\n";
 }return true;
 }
void SceneImages::draw(qlie::Bytes&out)const{
 if(out.size()!=1280*720*4)throw std::runtime_error("Scene canvas size");
 std::vector<const Layer*>sorted;
 for(auto&x:layers)if((x.second.pixels||x.second.composite)&&x.second.params.at("show")!="false")sorted.push_back(&x.second);
 auto get=[](const Layer*l,const std::string&k,float def){auto i=l->params.find(k);
 return i==l->params.end()?def:numeric(i->second);
 };
 std::stable_sort(sorted.begin(),sorted.end(),[&](auto*a,auto*b){return get(a,"pri",0)<get(b,"pri",0);});
 for(auto*l:sorted){Image generated;
 const Image*im=l->pixels.get();
 if(l->composite){auto m=l->params.find("motion");
 generated=l->composite->render(l->milliseconds,m==l->params.end()?"":m->second);
 im=&generated;
 }if(!im)continue;
 auto align=l->params.at("align");
 float ax=align.rfind("center",0)==0?0.5f:align.rfind("right",0)==0?1.f:0.f,ay=align.find(":bottom")!=align.npos?1.f:align.find(":center")!=align.npos?0.5f:0.f;
  blit(out,1280,720,*im,get(l,"x",640),get(l,"y",360),get(l,"scalex",100)/100,get(l,"scaley",100)/100,get(l,"rotz",0),ax,ay,get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)));
 }
}

qlie::Bytes SceneImages::save()const{StateWriter w;
 w.str("WHJ2-images-2");
 w.n(layers.size());
 for(auto&entry:layers){w.str(entry.first);
 auto&l=entry.second;
 w.str(l.prefix);
 w.str(l.path);
 w.n(uint64_t(l.milliseconds*1000));
 w.n(l.params.size());
 for(auto&p:l.params){w.str(p.first);
 w.str(p.second);
 }}return w.bytes;
 }
void SceneImages::restore(const qlie::Bytes&b){StateReader r{b};
 if(r.str()!="WHJ2-images-2")throw std::runtime_error("Image state version");
 std::map<std::string,Layer>next;
 auto count=r.n();
 if(count>256)throw std::runtime_error("Image layer count");
 while(count--){auto name=r.str();
 Layer l;
 l.prefix=r.str();
 l.path=r.str();
 l.milliseconds=r.n()/1000.;
 auto n=r.n();
 if(n>256)throw std::runtime_error("Image property count");
 while(n--){auto k=r.str();
 l.params[k]=r.str();
 }if(!l.path.empty()&&l.path.substr(l.path.size()-2)==".b"){auto it=composites.find(l.path);
 if(it==composites.end())it=composites.emplace(l.path,std::make_shared<Composite>(assets.read(l.path),l.path)).first;
 l.composite=it->second;
 }else if(!l.path.empty()){auto it=cache.find(l.path);
 if(it==cache.end())it=cache.emplace(l.path,std::make_shared<Image>(image(assets.read(l.path)))).first;
 l.pixels=it->second;
 }next[name]=std::move(l);
 }r.end();
 layers=std::move(next);
 }

}
