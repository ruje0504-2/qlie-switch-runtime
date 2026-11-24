// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene_images.hpp"
#include "story.hpp"
#include "state_io.hpp"
#include "animation.hpp"
#include "render_queue.hpp"
#include "gradient_mask.hpp"
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

const Motion* findMotion(const Animation&definition,const std::string&name){
 if(!name.empty()&&std::all_of(name.begin(),name.end(),[](unsigned char c){return c>='0'&&c<='9';})){
  auto index=std::stoul(name);return index<definition.motions.size()?&definition.motions[index]:nullptr;
 }
 for(auto&motion:definition.motions)if(motion.name==name)return &motion;
 if((name.empty()||name=="def")&&!definition.motions.empty())return &definition.motions.front();
 return nullptr;
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
 uint32_t identity=0;std::vector<uint32_t>motionIdentities;
 std::vector<Element>members;
 std::vector<qlie::Bytes>sounds;
 std::string path;
 mutable std::set<std::string>warnings;
 mutable std::map<size_t,std::unique_ptr<EmbeddedVideo>>clips;
 mutable std::map<size_t,Image>maskedImages;
 explicit Composite(const qlie::Bytes&bytes,const std::string&name,uint32_t&randomState):path(name){
  identity=particleRandomNext(randomState);
  bool metadata=false;
  for(auto&m:abmp(bytes)){
   if(m.type.rfind("abdata",0)==0){definition=animation(m.type,m.data);
 for(size_t i=0;i<definition.motions.size();i++)motionIdentities.push_back(particleRandomNext(randomState));
 metadata=true;
 }
   else if(m.type.rfind("abimg",0)==0){Element e;
 if(!m.data.empty()){
    if(m.data.size()>=5&&std::equal(m.data.begin(),m.data.begin()+5,"abmp1"))e.nested=std::make_shared<Composite>(m.data,name+"#"+m.name,randomState);
    else if(m.data.size()>=4&&std::equal(m.data.begin(),m.data.begin()+4,"OggS")){e.video=std::move(m.data);
 }
    else if(m.data.size()>=12&&std::equal(m.data.begin(),m.data.begin()+12,"ARGBModel1.0"))warn("model="+m.name);
    else e.image=std::make_shared<Image>(image(m.data));
   }members.push_back(std::move(e));
 }
   else if(m.type.rfind("absnd",0)==0)sounds.push_back(std::move(m.data));
  }
  if(!metadata)throw std::runtime_error("Composite lacks animation metadata "+name);
  for(auto&motion:definition.motions)for(auto&layer:motion.layers){if(layer.mode>2)warn("layer-mode="+std::to_string(layer.mode));
 for(auto&k:layer.keys)if(k[4]!=0&&k[4]!=1&&k[4]!=4&&k[4]!=10&&k[4]!=15&&k[4]!=16&&k[4]!=20)warn("primitive="+std::to_string(k[4]));
 }
 }
 void saveIdentity(StateWriter&w)const{w.n(identity);w.n(motionIdentities.size());for(auto v:motionIdentities)w.n(v);for(auto&e:members)if(e.nested)e.nested->saveIdentity(w);}
 void restoreIdentity(StateReader&r){identity=uint32_t(r.n());auto count=r.n();if(count!=motionIdentities.size())throw std::runtime_error("Composite identity motion count");for(auto&v:motionIdentities)v=uint32_t(r.n());for(auto&e:members)if(e.nested)e.nested->restoreIdentity(r);}
 void warn(const std::string&s)const{if(warnings.insert(s).second)std::cerr<<"PENDING-ANIMATION "<<path<<" "<<s<<"\n";
 }
 void enqueue(RenderQueue&queue,double ms,const std::string&motionName,const Affine2D&parent,float depth,float opacity,uint32_t parentAdd,uint32_t parentMul,unsigned parentBlend=0,const Image*replacement=nullptr)const{
  if(definition.bounds[0]<0||definition.bounds[1]<0||definition.bounds[0]>4096||definition.bounds[1]>4096)throw std::runtime_error("Composite bounds "+path);
  const Motion*motion=findMotion(definition,motionName);
 if(!motion)throw std::runtime_error("Missing motion "+motionName+" in "+path);
  auto randomSeed=0x35246u^identity^motionIdentities[size_t(motion-&definition.motions[0])];
  randomSeed=((randomSeed&0xffff)*(randomSeed>>16))&1023;
  auto samples=sampleMotion(*motion,ms,1280,720,false,AnimationContext{unsigned(definition.bounds[0]),unsigned(definition.bounds[1]),1280,720,randomSeed});
  for(size_t index=0;index<samples.size();index++){
 auto&k=samples[index];
 if(parentBlend)k.flags=parentBlend;
 for(auto&w:k.unsupported)if(w!="primitive=20")warn(w);
 if(!motion->layers[index].flags||!k.kind)continue;
 k.alpha=int(k.alpha)*int(opacity)/255;k.addColor=colorAdd(k.addColor,parentAdd);k.mulColor=colorMultiply(k.mulColor,parentMul);
 if(k.kind==15||k.kind==16){queue.control(k,depth+k.priority);continue;}
 if(k.kind==1){
  if((k.flags&0x7f)>8){warn("rectangle-blend="+std::to_string(k.flags));continue;}
  if(std::abs(k.scaleX)>=32700||std::abs(k.scaleY)>=32700){warn("rectangle-symbolic-size");continue;}
  // PE eaff0/ea798: rectangle dimensions are pixels, not image scale percentages.
  static const Image white{1,1,{255,255,255,255}};
  auto shape=imagePlacement(k.x,k.y,k.scaleX,k.scaleY,k.rotationZ,k.anchorX/100,k.anchorY/100,1,1);
  queue.image(white,affine(parent,shape),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f);
  continue;
 }
 if(k.kind==10){
  if(k.resource>=members.size()||k.childMotion>=members.size())throw std::runtime_error("Mask image index "+path);
  const auto&source=members[k.resource].image;const auto&mask=members[k.childMotion].image;
  if(!source||!mask){warn("mask-nonbitmap-resource");continue;}
  bool rotated=false;for(auto&w:k.unsupported)if(w=="3d-rotation")rotated=true;
  if(rotated){warn("mask-3d-rotation");continue;}
  auto&output=maskedImages[index];output.width=source->width;output.height=source->height;output.pixels.resize(source->pixels.size());
  float width=k.mask.width?k.mask.width:source->width,height=k.mask.height?k.mask.height:source->height;
  float maskWidth=k.mask.maskWidth?k.mask.maskWidth:mask->width,maskHeight=k.mask.maskHeight?k.mask.maskHeight:mask->height;
  auto pixel=[](float value,unsigned size){return unsigned(std::clamp(int(std::floor(value)),0,int(size)-1));};
  for(unsigned y=0;y<output.height;y++)for(unsigned x=0;x<output.width;x++){
   float u=(x+.5f)/output.width,v=(y+.5f)/output.height;
   unsigned sx=pixel(k.mask.sourceX-width*k.mask.alignX+u*width,source->width),sy=pixel(k.mask.sourceY-height*k.mask.alignY+v*height,source->height);
   unsigned mx=pixel(k.mask.x-maskWidth*k.mask.alignX+u*maskWidth,mask->width),my=pixel(k.mask.y-maskHeight*k.mask.alignY+v*maskHeight,mask->height);
   auto src=source->pixels.data()+(size_t(sy)*source->width+sx)*4;auto dst=output.pixels.data()+(size_t(y)*output.width+x)*4;
   std::copy(src,src+4,dst);dst[3]=gradientMaskAlpha(src[3],mask->pixels[(size_t(my)*mask->width+mx)*4],k.mask.amount,k.mask.mode);
  }
  auto placement=imagePlacement(k.x,k.y,k.scaleX/100*width/source->width,k.scaleY/100*height/source->height,k.rotationZ,k.anchorX/100,k.anchorY/100,source->width,source->height);
  queue.snapshot(output,affine(parent,placement),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f);
  continue;
 }
 if(k.kind==20){
  if(k.resource>=members.size())throw std::runtime_error("Particle image index "+path);
  auto&e=members[k.resource];
  if(!e.image){warn("particle-nested-or-video pending");continue;}
  bool rotation3d=false;for(auto&w:k.unsupported)if(w=="3d-rotation")rotation3d=true;
  if(rotation3d){warn("particle-3d-rotation pending");continue;}
  auto seed=0x35246u^identity^motionIdentities[size_t(motion-&definition.motions[0])];
  seed=((seed&0xffff)*(seed>>16))&1023;
  for(auto&point:particlePoints(k.particles,seed)){
   auto pos=projectParticle(point,k.particles,k.x,k.y,k.anchorX/100,k.anchorY/100,k.rotationZ,1280,720,e.image->width,e.image->height);
   if(!pos.visible)continue;
   auto placement=imagePlacement(pos.x,pos.y,pos.scale,pos.scale,k.rotationZ,.5f,.5f,e.image->width,e.image->height);
   queue.image(*e.image,affine(parent,placement),depth+k.priority,k.alpha*point.alpha/255,k.addColor,k.mulColor,k.flags&0x7f);
  }
  continue;
 }
 if(k.kind!=4)continue;
 if(k.resource>=members.size())throw std::runtime_error("Composite image index "+path);
   auto&e=members[k.resource];
 const Image*source=replacement&&k.resource==0?replacement:e.image.get();bool dynamic=false;
   if(replacement&&k.resource==0){
    queue.image(*source,affine(parent,imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,source->width,source->height)),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f);continue;
   }
   if(e.nested){
 if(!k.childTimeResolved){warn("nested-time context unresolved");continue;}
 if(k.childMotion>=e.nested->definition.motions.size())throw std::runtime_error("Nested motion index "+path);
 auto local=imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,unsigned(e.nested->definition.bounds[0]),unsigned(e.nested->definition.bounds[1]));
 e.nested->enqueue(queue,nestedAnimationTime(e.nested->definition,k.childTime),e.nested->definition.motions[k.childMotion].name,affine(parent,local),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f);
 continue;
 }
   else if(!e.video.empty()){
    auto&clip=clips[index];
 if(!clip||&clip->bytes!=&e.video){clip=std::make_unique<EmbeddedVideo>(e.video);
 if(kvideo_has_audio(clip->video))warn("video-audio requires mixer binding");
 }
    warn("video-member origin alignment unverified");
 source=&clip->at(ms);dynamic=true;
   }
   if(source){auto place=affine(parent,imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,source->width,source->height));
 if(dynamic)queue.snapshot(*source,place,depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f);
 else queue.image(*source,place,depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f);
 }
  }
 }
};
SceneImages::SceneImages(qlie::Assets&a):assets(a){for(auto type:{"image","chara","face"}){std::istringstream in(qlie::cp932(a.read(std::string("avg/imoscripter_template_")+type+".txt")));
 std::string s;
 while(std::getline(in,s)){auto p=s.find(61);
 if(p!=s.npos)templates[type][trim(s.substr(0,p))]=trim(s.substr(p+1));
 }}
 auto table=[&](const std::string&path){std::map<std::string,std::string> out;std::istringstream in(qlie::cp932(a.read(path)));std::string line;while(std::getline(in,line)){auto pos=line.find('=');if(pos!=line.npos)out[trim(line.substr(0,pos))]=trim(line.substr(pos+1));}return out;};
 faceRepId=table("avg/imoscripter_face_repid.txt");faceRepFile=table("avg/imoscripter_face_repfile.txt");auto project=table("avg/projectsetting.txt");
 if(project.count("FaceType"))faceType=unsigned(std::stoul(project.at("FaceType")));
 if(project.count("FaceDefaultFile"))faceDefaultFile=project.at("FaceDefaultFile");
}
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
std::vector<SceneSoundEvent> SceneImages::takeSoundEvents(){
 std::vector<SceneSoundEvent> result;
 for(auto&entry:layers){auto&layer=entry.second;
  int base=layer.params.count("dchannel")?std::stoi(layer.params.at("dchannel")):0;
  if(base<0||base>=16)throw std::runtime_error("Direct sound channel out of range");
  int volume=layer.params.count("vol")?int(std::stof(layer.params.at("vol"))*256/100):256;
  if(layer.audioStopPending){SceneSoundEvent sound;sound.stop=true;sound.channel=8+unsigned(base);result.push_back(sound);layer.audioStopPending=false;}
  if(!layer.composite)continue;
  auto&object=*layer.composite;auto i=layer.params.find("motion");
  const Motion*motion=findMotion(object.definition,i==layer.params.end()?"":i->second);
  if(!motion)continue;
  for(auto event:animationSoundEvents(*motion,layer.audioMilliseconds,layer.milliseconds)){
   if(unsigned(base)+event.channel>=16)throw std::runtime_error("Embedded sound channel out of range");
   SceneSoundEvent sound; sound.stop=event.stop;sound.loop=event.loop;sound.channel=8+unsigned(base)+event.channel;sound.gain=std::clamp(int(event.gain*256)*volume/256,0,256)/256.f;
   if(!event.stop&&sound.gain==0)continue;
   if(!event.stop){if(event.resource>=object.sounds.size()||object.sounds[event.resource].empty())throw std::runtime_error("Embedded sound resource missing "+layer.path);
    sound.source="@abmp:"+std::to_string(event.resource)+":"+layer.path;sound.bytes=object.sounds[event.resource];
   }result.push_back(std::move(sound));
  }layer.audioMilliseconds=layer.milliseconds;
 }return result;
}
float SceneImages::value(const Layer&layer,const std::string&key,float fallback){
 auto tween=layer.tweens.find(key);
 if(tween==layer.tweens.end()){auto param=layer.params.find(key);return param==layer.params.end()?fallback:numeric(param->second);}
 const auto&t=tween->second;double q=t.duration>0?std::clamp(t.elapsed/t.duration,0.,1.):1;
 double exponent=1+1.4*t.power/100;
 if(q<1)switch(t.mode){case 0:q=0;break;case 1:break;case 3:q=1-std::pow(1-q,exponent);break;case 4:q=std::pow(q,exponent);break;default:throw std::runtime_error("Unsupported image tween mode");}
 if(key=="mulcolor"||key=="addcolor"){uint32_t result=0;for(unsigned shift=0;shift<24;shift+=8){int from=(uint32_t(t.from)>>shift)&255,to=(uint32_t(t.to)>>shift)&255;result|=uint32_t(from+(to-from)*q)<<shift;}return float(result);}
 return float(t.from+(t.to-t.from)*q);
}
void SceneImages::transition(Layer&layer,const Layer&before,bool position,bool color){
 double duration=layer.params.count("time")?numeric(layer.params.at("time")):0;
 unsigned mode=1;auto m=layer.params.find("move");if(m!=layer.params.end()){
  if(m->second=="step")mode=0;else if(m->second=="linear")mode=1;else if(m->second=="outquart"||m->second=="square")mode=3;else if(m->second=="inquart"||m->second=="invsqare")mode=4;
  else {std::cerr<<"PENDING-IMAGE-TWEEN mode="<<m->second<<"\n";mode=0;}
 }
 float power=layer.params.count("movepow")?numeric(layer.params.at("movepow")):100;
 auto add=[&](const std::string&key,float fallback){float from=value(before,key,fallback);auto found=layer.params.find(key);float to=found==layer.params.end()?fallback:numeric(found->second);
  if(key=="x"||key=="y"){auto axial=layer.params.find(key=="x"?"ax":"ay");if(axial!=layer.params.end())to+=numeric(axial->second);}
  if(duration>0&&from!=to)layer.tweens[key]={from,to,power,0,duration,mode};else {layer.tweens.erase(key);if(key=="x"||key=="y")layer.tweens[key]={to,to,power,0,0,mode};}
 };
 if(position){add("x",640);add("y",360);add("scalex",100);add("scaley",100);add("rotz",0);add("pri",0);}
 if(color){add("alpha",255);add("addcolor",0);add("mulcolor",0xffffff);}
}
void SceneImages::update(double ms){for(auto&entry:layers){auto&layer=entry.second;
 double speed=layer.params.count("speed")?numeric(layer.params.at("speed")):100;
 layer.milliseconds+=ms*std::max(0.,speed)/100;
 for(auto&t:layer.tweens)t.second.elapsed=std::min(t.second.duration,t.second.elapsed+ms);
 }}
bool SceneImages::command(const std::string&line){auto p=line.find(44);
 auto cmd=line.substr(1,p-1);
 bool chara=cmd.rfind("chara",0)==0,face=cmd=="face";
 if(!chara&&!face&&cmd.rfind("bg",0)!=0&&cmd!="effect"&&cmd!="env")return false;
 auto&l=layers[cmd];
 const std::map<std::string,std::string> faceDefaults{{"mode","auto"},{"disp","once"},{"show","false"},{"x","0"},{"y","0"},{"scalex","100"},{"scaley","100"},{"alpha","255"},{"pri","0"},{"align","left:top"},{"@face-active","false"}};
 if(face&&l.params.empty())l.params=faceDefaults;
 if(l.params.empty())l.params={{"x","640"},{"y",chara?"720":"360"},{"scalex","100"},{"scaley","100"},{"alpha","255"},{"pri",chara?"500":"0"},{"show","true"},{"align",chara?"center:bottom":cmd=="effect"?"left:top":"center:center"}};
 if(chara){auto motion=l.params.find("motion");if(motion!=l.params.end()&&motion->second!="def"&&motion->second!="0"&&motion->second.find('+')==std::string::npos){motion->second="def";l.milliseconds=0;l.audioMilliseconds=-1;}}
 auto args=split(expand(p==line.npos?"":line.substr(p+1),chara?"chara":face?"face":"image"),44);
 Layer before=l;bool file=false,baseChanged=false,position=false,color=false;
 for(auto&s:args){if(s.empty())continue;
 auto c=s.find(58);
 if(c==s.npos)throw std::runtime_error("Unknown image template "+s);
 auto key=s.substr(0,c),v=s.substr(c+1);
 if(face&&key=="clear"&&v=="def"){l=Layer{};l.params=faceDefaults;continue;}
 if(key=="clear"){l.params.erase("@portrait");l.path.clear();
 l.pixels.reset();
 l.composite.reset();
 l.params["show"]="false";
 continue;
 }l.params[key]=v;
 if(key=="file"){file=true;if(face)l.params["@face-active"]="true";}
 if((chara||face)&&key=="base")baseChanged=true;
 if(key=="x"||key=="y"||key=="ax"||key=="ay"||key=="scalex"||key=="scaley"||key=="rotz"||key=="pri")position=true;
 if(key=="alpha"||key=="mulcolor"||key=="addcolor")color=true;
 if(key=="vol"&&std::stof(v)==0)l.audioStopPending=true;
 if(key=="motion"){l.milliseconds=0;l.audioMilliseconds=-1;}
 if(key!="id"&&key!="mode"&&key!="disp"&&key!="base"&&key!="time"&&key!="speed"&&key!="move"&&key!="movepow"&&key!="ax"&&key!="ay"&&key!="vol"&&key!="dchannel"&&key!="motion"&&key!="file"&&key!="x"&&key!="y"&&key!="scalex"&&key!="scaley"&&key!="alpha"&&key!="show"&&key!="pri"&&key!="align"&&key!="texfilter"&&key!="rotz"&&key!="mulcolor"&&key!="addcolor")std::cerr<<"PENDING-IMAGE-PROPERTY "<<cmd<<" "<<key<<"="<<v<<"\n";
 }
 transition(l,before,position,color);
 auto loadBase=[&](){auto base=l.params.find("base");if((!chara&&!face)||base==l.params.end()||base->second=="none"||base->second.empty())return false;
  std::string path=(face?"face/":"chara/")+base->second;if(!assets.exists(path)&&assets.exists(path+".b"))path+=".b";
  if(!assets.exists(path))throw std::runtime_error("Missing character base "+path);
  auto it=composites.find(path);if(it==composites.end())it=composites.emplace(path,std::make_shared<Composite>(assets.read(path),path,particleState)).first;
  l.composite=it->second;l.path=path;l.milliseconds=0;l.audioMilliseconds=-1;return true;
 };
 if(baseChanged&&!file){if(!loadBase()){l.composite.reset();auto portrait=l.params.find("@portrait");l.path=portrait==l.params.end()?"":portrait->second;}return true;}
 if(file){std::string f=l.params["file"];
 if(f=="none"||f=="none:none"){l.params.erase("@portrait");l.path.clear();
 l.pixels.reset();
 l.composite.reset();
 return true;
 }if(chara){auto pair=split(f,58);
 if(pair.size()>1)l.prefix=pair[1];
 f=pair[0].find(47)!=std::string::npos?pair[0]:l.prefix+pair[0];
 }std::string path=(chara?"chara/":face?"face/":"image/")+f;
 if(!assets.exists(path)){if(assets.exists(path+".png"))path+=".png";
 else if(assets.exists(path+".b"))path+=".b";
 else throw std::runtime_error("Missing image "+path);
 }l.path=path;
 if(chara||face)l.params["@portrait"]=path;
 if(path.size()>2&&path.substr(path.size()-2)==".b"){l.pixels.reset();
 auto it=composites.find(path);
 if(it==composites.end())it=composites.emplace(path,std::make_shared<Composite>(assets.read(path),path,particleState)).first;
 l.composite=it->second;
 l.milliseconds=0;
 l.audioMilliseconds=-1;
 return true;
 }auto it=cache.find(path);
 if(it==cache.end())it=cache.emplace(path,std::make_shared<Image>(image(assets.read(path)))).first;
 l.pixels=it->second;
 if(loadBase())return true;
 l.composite.reset();
 l.milliseconds=0;
 l.audioMilliseconds=-1;
 std::cerr<<"IMAGE loaded "<<path<<" "<<l.pixels->width<<"x"<<l.pixels->height<<"\n";
 }return true;
 }
void SceneImages::beginPage(){faceWarning=false;auto i=layers.find("face");if(i!=layers.end())i->second.params["@face-active"]="false";}
void SceneImages::selectFace(const std::string&speaker,const std::string&voice){
 auto face=layers.find("face");if(face==layers.end())return;auto&params=face->second.params;
 if(params.at("mode")!="auto"||params.at("show")!="true"||params.at("@face-active")=="true")return;
 faceWarning=true;std::string id=faceType?voice:speaker;
 if(id.rfind("【",0)==0){id.erase(0,std::string("【").size());auto end=id.find("】");if(end!=id.npos)id.resize(end);}
 if(id.rfind("％",0)==0)id.erase(0,std::string("％").size());
 for(auto&entry:faceRepId)if(id.find(entry.first)!=id.npos){id=entry.second;break;}
 auto suffix=id.find("＠");if(suffix!=id.npos)id.resize(suffix);if(id.empty())return;
 std::string file;
 for(auto&entry:layers)if(entry.first.rfind("chara",0)==0&&entry.second.params.count("id")&&entry.second.params.at("id")==id&&entry.second.params.count("@portrait")){file=entry.second.params.at("@portrait");break;}
 if(file.empty())for(auto&entry:layers)if(entry.first.rfind("chara",0)==0&&entry.second.params.count("@portrait")&&entry.second.params.at("@portrait").find(id)!=id.npos){file=entry.second.params.at("@portrait");break;}
 if(file.empty()){return;}
 if(file.rfind("chara/",0)==0)file.erase(0,6);
 for(auto&entry:faceRepFile){size_t pos=0;while((pos=file.find(entry.first,pos))!=file.npos){file.replace(pos,entry.first.size(),entry.second);pos+=entry.second.size();}}
 auto resolve=[&](std::string name){if(assets.exists("face/"+name))return name;for(auto ext:{".png",".b",".argb"})if(assets.exists("face/"+name+ext))return name+ext;return std::string{};};
 auto resolved=resolve(file);
 if(resolved.empty()&&!faceDefaultFile.empty()){auto fallback=faceDefaultFile;size_t pos;while((pos=fallback.find("[id]"))!=fallback.npos)fallback.replace(pos,4,id);resolved=resolve(fallback);}
 if(resolved.empty()){std::cerr<<"PENDING-FACE unresolved candidate="<<file<<"\n";return;}
 command("^face,file:"+resolved);
}
void SceneImages::draw(qlie::Bytes&out)const{render(out,0);}
void SceneImages::drawScene(qlie::Bytes&out)const{render(out,1);}
void SceneImages::drawFaces(qlie::Bytes&out)const{render(out,2);}
void SceneImages::render(qlie::Bytes&out,unsigned pass)const{
 if(out.size()!=1280*720*4)throw std::runtime_error("Scene canvas size");
 std::vector<const Layer*>sorted;
 for(auto&x:layers){auto&layer=x.second;bool face=x.first=="face";
 if((pass==1&&face)||(pass==2&&!face))continue;
 if(face&&layer.params.at("disp")!="keep"&&layer.params.at("@face-active")!="true"){
 if(layer.params.at("show")=="true"&&layer.params.at("mode")=="auto"&&!faceWarning){std::cerr<<"PENDING-FACE automatic portrait selection\n";faceWarning=true;}continue;
 }
 auto base=layer.params.find("base");
 if(base!=layer.params.end()&&!base->second.empty()&&base->second!="none"&&!layer.params.count("@portrait"))continue;
 if((layer.pixels||layer.composite)&&layer.params.at("show")!="false")sorted.push_back(&layer);
 }
 auto get=[](const Layer*l,const std::string&k,float def){return value(*l,k,def);};
 std::stable_sort(sorted.begin(),sorted.end(),[&](auto*a,auto*b){return get(a,"pri",0)<get(b,"pri",0);});
 RenderQueue queue;
 for(auto*l:sorted){
 auto align=l->params.at("align");
 float ax=align.rfind("center",0)==0?0.5f:align.rfind("right",0)==0?1.f:0.f,ay=align.find(":bottom")!=align.npos?1.f:align.find(":center")!=align.npos?0.5f:0.f;
 unsigned w=l->composite?unsigned(l->composite->definition.bounds[0]):l->pixels->width,h=l->composite?unsigned(l->composite->definition.bounds[1]):l->pixels->height;
 auto placement=imagePlacement(get(l,"x",640),get(l,"y",360),get(l,"scalex",100)/100,get(l,"scaley",100)/100,get(l,"rotz",0),ax,ay,w,h);
 if(l->composite){auto m=l->params.find("motion");l->composite->enqueue(queue,l->milliseconds,m==l->params.end()?"":m->second,placement,get(l,"pri",0),get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)),0,l->params.count("@portrait")?l->pixels.get():nullptr);}
 else queue.image(*l->pixels,placement,get(l,"pri",0),get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)));
 }
 queue.draw(out,1280,720);
}

qlie::Bytes SceneImages::save()const{StateWriter w;
 w.str("WHJ2-images-6");
 w.n(particleState);
 w.n(layers.size());
 for(auto&entry:layers){w.str(entry.first);
 auto&l=entry.second;
 w.str(l.prefix);
 w.str(l.path);
 w.n(uint64_t(l.milliseconds*1000));
 w.n(l.audioMilliseconds<0?0:uint64_t(l.audioMilliseconds*1000)+1);
 w.n(l.audioStopPending);
 w.n(l.tweens.size());for(auto&t:l.tweens){w.str(t.first);w.f(t.second.from);w.f(t.second.to);w.f(t.second.power);w.n(uint64_t(t.second.elapsed*1000));w.n(uint64_t(t.second.duration*1000));w.n(t.second.mode);}
 w.n(l.params.size());
 for(auto&p:l.params){w.str(p.first);
 w.str(p.second);
 }if(l.composite)l.composite->saveIdentity(w);
 }return w.bytes;
 }
void SceneImages::restore(const qlie::Bytes&b){StateReader r{b};
 auto version=r.str();bool v6=version=="WHJ2-images-6",v5=v6||version=="WHJ2-images-5",v4=v5||version=="WHJ2-images-4",v3=v4||version=="WHJ2-images-3";
 if(!v3&&version!="WHJ2-images-2")throw std::runtime_error("Image state version");
 auto savedParticleState=v3?uint32_t(r.n()):particleState;
 std::map<std::string,Layer>next;
 auto count=r.n();
 if(count>256)throw std::runtime_error("Image layer count");
 // Cached future assets must not reuse identities from the discarded timeline.
 composites.clear();
 while(count--){auto name=r.str();
 Layer l;
 l.prefix=r.str();
 l.path=r.str();
 l.milliseconds=r.n()/1000.;
 if(v4){auto cursor=r.n();l.audioMilliseconds=cursor?(cursor-1)/1000.:-1;}else l.audioMilliseconds=l.milliseconds;
 if(v5)l.audioStopPending=r.n()!=0;
 if(v6){auto count=r.n();if(count>16)throw std::runtime_error("Image tween count");while(count--){auto key=r.str();Tween t;t.from=r.f();t.to=r.f();t.power=r.f();t.elapsed=r.n()/1000.;t.duration=r.n()/1000.;t.mode=unsigned(r.n());l.tweens[key]=t;}}
 auto n=r.n();
 if(n>256)throw std::runtime_error("Image property count");
 while(n--){auto k=r.str();
 l.params[k]=r.str();
 }if(!l.path.empty()&&l.path.substr(l.path.size()-2)==".b"){auto it=composites.find(l.path);
 if(it==composites.end())it=composites.emplace(l.path,std::make_shared<Composite>(assets.read(l.path),l.path,particleState)).first;
 l.composite=it->second;
 }else if(!l.path.empty()){auto it=cache.find(l.path);
 if(it==cache.end())it=cache.emplace(l.path,std::make_shared<Image>(image(assets.read(l.path)))).first;
 l.pixels=it->second;
 }if(l.composite&&l.params.count("@portrait")&&l.path!=l.params.at("@portrait")){auto portrait=l.params.at("@portrait");auto it=cache.find(portrait);if(it==cache.end())it=cache.emplace(portrait,std::make_shared<Image>(image(assets.read(portrait)))).first;l.pixels=it->second;}
 if(v3&&l.composite)l.composite->restoreIdentity(r);
 next[name]=std::move(l);
 }r.end();
 layers=std::move(next);
 particleState=savedParticleState;
 }

}
