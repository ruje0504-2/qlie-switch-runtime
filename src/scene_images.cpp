// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene_images.hpp"
#include "story.hpp"
#include "state_io.hpp"
#include "animation.hpp"
#include "render_queue.hpp"
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
 void enqueue(RenderQueue&queue,double ms,const std::string&motionName,const Affine2D&parent,float depth,float opacity,uint32_t parentAdd,uint32_t parentMul)const{
  if(definition.bounds[0]<=0||definition.bounds[1]<=0||definition.bounds[0]>4096||definition.bounds[1]>4096)throw std::runtime_error("Composite bounds "+path);
  const Motion*motion=nullptr;
 for(auto&m:definition.motions)if(m.name==motionName)motion=&m;
 if(!motion&&(motionName.empty()||motionName=="def"||motionName=="0")&&!definition.motions.empty())motion=&definition.motions.front();
 if(!motion)throw std::runtime_error("Missing motion "+motionName+" in "+path);
  auto samples=sampleMotion(*motion,ms,1280,720,false);
  for(size_t index=0;index<samples.size();index++){
 auto&k=samples[index];
 for(auto&w:k.unsupported)warn(w);
 if(!motion->layers[index].flags||!k.kind)continue;
 k.alpha=int(k.alpha)*int(opacity)/255;k.addColor=colorAdd(k.addColor,parentAdd);k.mulColor=colorMultiply(k.mulColor,parentMul);
 if(k.kind==15||k.kind==16){queue.control(k,depth+k.priority);continue;}
 if(k.kind!=4)continue;
 if(k.resource>=members.size())throw std::runtime_error("Composite image index "+path);
   auto&e=members[k.resource];
 const Image*source=e.image.get();
   if(e.nested){warn("nested-motion origin alignment unverified");
 auto local=imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,unsigned(e.nested->definition.bounds[0]),unsigned(e.nested->definition.bounds[1]));
 e.nested->enqueue(queue,ms,"",affine(parent,local),depth+k.priority,k.alpha,k.addColor,k.mulColor);
 continue;
 }
   else if(!e.video.empty()){
    auto&clip=clips[index];
 if(!clip||&clip->bytes!=&e.video){clip=std::make_unique<EmbeddedVideo>(e.video);
 if(kvideo_has_audio(clip->video))warn("video-audio requires mixer binding");
 }
    warn("video-member origin alignment unverified");
 source=&clip->at(ms);
   }
   if(source)queue.image(*source,affine(parent,imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,source->width,source->height)),depth+k.priority,k.alpha,k.addColor,k.mulColor);
  }
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
 if(key!="motion"&&key!="file"&&key!="x"&&key!="y"&&key!="scalex"&&key!="scaley"&&key!="alpha"&&key!="show"&&key!="pri"&&key!="align"&&key!="texfilter"&&key!="rotz"&&key!="mulcolor"&&key!="addcolor")std::cerr<<"PENDING-IMAGE-PROPERTY "<<cmd<<" "<<key<<"="<<v<<"\n";
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
 RenderQueue queue;
 for(auto*l:sorted){
 auto align=l->params.at("align");
 float ax=align.rfind("center",0)==0?0.5f:align.rfind("right",0)==0?1.f:0.f,ay=align.find(":bottom")!=align.npos?1.f:align.find(":center")!=align.npos?0.5f:0.f;
 unsigned w=l->composite?unsigned(l->composite->definition.bounds[0]):l->pixels->width,h=l->composite?unsigned(l->composite->definition.bounds[1]):l->pixels->height;
 auto placement=imagePlacement(get(l,"x",640),get(l,"y",360),get(l,"scalex",100)/100,get(l,"scaley",100)/100,get(l,"rotz",0),ax,ay,w,h);
 if(l->composite){auto m=l->params.find("motion");l->composite->enqueue(queue,l->milliseconds,m==l->params.end()?"":m->second,placement,get(l,"pri",0),get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)));}
 else queue.image(*l->pixels,placement,get(l,"pri",0),get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)));
 }
 queue.draw(out,1280,720);
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
