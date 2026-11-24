// SPDX-License-Identifier: GPL-2.0-or-later
#include "scene_images.hpp"
#include "story.hpp"
#include "state_io.hpp"
#include "animation.hpp"
#include "render_queue.hpp"
#include "gradient_mask.hpp"
#include "model.hpp"
#include <set>
extern "C" {
#include "video.h"
}
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cmath>
#include <cstring>
namespace whj2 {
namespace {
float numeric(const std::string&s){return !s.empty()&&s[0]=='$'?float(std::stoul(s.substr(1),nullptr,16)):std::stof(s);
 }

struct Matrix3D {
 float m[3][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0}};
 Vec3 point(Vec3 v)const{return {m[0][0]*v.x+m[0][1]*v.y+m[0][2]*v.z+m[0][3],m[1][0]*v.x+m[1][1]*v.y+m[1][2]*v.z+m[1][3],m[2][0]*v.x+m[2][1]*v.y+m[2][2]*v.z+m[2][3]};}
};
Matrix3D combine(const Matrix3D&a,const Matrix3D&b){Matrix3D r;for(unsigned i=0;i<3;i++)for(unsigned j=0;j<4;j++){r.m[i][j]=j==3?a.m[i][3]:0;for(unsigned k=0;k<3;k++)r.m[i][j]+=a.m[i][k]*b.m[k][j];}return r;}
Matrix3D from2D(const Affine2D&a){Matrix3D r;r.m[0][0]=a.a;r.m[0][1]=a.b;r.m[0][3]=a.x;r.m[1][0]=a.c;r.m[1][1]=a.d;r.m[1][3]=a.y;return r;}
Matrix3D placement3D(const AnimationSample&k,unsigned width,unsigned height){
 constexpr float radians=3.14159265358979323846f/180;
 float cx=std::cos(k.rotationX*radians),sx=std::sin(k.rotationX*radians),cy=std::cos(k.rotationY*radians),sy=std::sin(k.rotationY*radians),cz=std::cos(k.rotationZ*radians),sz=std::sin(k.rotationZ*radians);
 Matrix3D x,y,z,scale,anchor,position;
 x.m[1][1]=cx;x.m[1][2]=-sx;x.m[2][1]=sx;x.m[2][2]=cx;
 y.m[0][0]=cy;y.m[0][2]=sy;y.m[2][0]=-sy;y.m[2][2]=cy;
 z.m[0][0]=cz;z.m[0][1]=-sz;z.m[1][0]=sz;z.m[1][1]=cz;
 scale.m[0][0]=k.scaleX/100;scale.m[1][1]=k.scaleY/100;scale.m[2][2]=k.scaleZ/100;
 anchor.m[0][3]=-float(width)*k.anchorX/100;anchor.m[1][3]=-float(height)*k.anchorY/100;
 position.m[0][3]=k.x;position.m[1][3]=k.y;position.m[2][3]=-k.z;
 // PE ea798/a14ec uses row vectors: anchor, scale, axis rotations, position.
 static constexpr unsigned orders[6][3]={{0,1,2},{0,2,1},{1,0,2},{1,2,0},{2,0,1},{2,1,0}};
 if(k.rotationOrder>=6)throw std::runtime_error("Invalid rotation order");
 Matrix3D axes[3]={x,y,z},rotation;
 for(unsigned axis:orders[k.rotationOrder])rotation=combine(axes[axis],rotation);
 return combine(position,combine(rotation,combine(scale,anchor)));
}
void quad(RenderQueue&queue,const Image&texture,const Matrix3D&matrix,const AnimationSample&k,float priority){
 std::array<TextureVertex,4> v;for(unsigned i=0;i<4;i++){float x=i==1||i==2?texture.width:0,y=i>=2?texture.height:0;auto p=matrix.point({x,y,0});v[i]={p.x,p.y,1000-p.z,x,y};}
 queue.triangle(texture,{v[0],v[1],v[2]},priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);
 queue.triangle(texture,{v[0],v[2],v[3]},priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);
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
 bool ended=false,audible=false;
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
 ended=false;audible=false;
 }
 const Image&at(double ms){uint64_t target=uint64_t(std::max(0.,ms)*60/1000);
 if(target+1<ticks)reset();
 while(!ended&&ticks<=target){KImage dst{0,0,frame.width,frame.height,size_t(frame.width)*4,frame.pixels.data()};
 uint8_t*pcm=nullptr;
 size_t count=0;
 int code=kvideo_step(video,&dst,&pcm,&count);
 if(pcm&&count&&std::any_of(pcm,pcm+count,[](uint8_t sample){return sample!=0;}))audible=true;
 free(pcm);
 ticks++;
 if(code<0)throw std::runtime_error(kvideo_error(video));
 ended=code==1;
 }return frame;
 }
};
struct Composite {
 struct Element {std::shared_ptr<Image>image;std::shared_ptr<Model>mesh;std::string name;
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
   else if(m.type.rfind("abimg",0)==0){Element e;e.name=m.name;
 if(!m.data.empty()){
    if(m.data.size()>=5&&std::equal(m.data.begin(),m.data.begin()+5,"abmp1"))e.nested=std::make_shared<Composite>(m.data,name+"#"+m.name,randomState);
    else if(m.data.size()>=4&&std::equal(m.data.begin(),m.data.begin()+4,"OggS")){e.video=std::move(m.data);
 }
    else if(m.data.size()>=12&&std::equal(m.data.begin(),m.data.begin()+12,"ARGBModel1.0"))e.mesh=std::make_shared<Model>(model(m.data));
    else e.image=std::make_shared<Image>(image(m.data));
   }members.push_back(std::move(e));
 }
   else if(m.type.rfind("absnd",0)==0)sounds.push_back(std::move(m.data));
  }
  if(!metadata)throw std::runtime_error("Composite lacks animation metadata "+name);
  for(auto&motion:definition.motions)for(auto&layer:motion.layers){if(layer.mode>2)warn("layer-mode="+std::to_string(layer.mode));
 for(auto&k:layer.keys)if(k[4]!=0&&k[4]!=1&&k[4]!=4&&k[4]!=6&&k[4]!=10&&k[4]!=15&&k[4]!=16&&k[4]!=17&&k[4]!=18&&k[4]!=20)warn("primitive="+std::to_string(k[4]));
 }
 }
 void saveIdentity(StateWriter&w)const{w.n(identity);w.n(motionIdentities.size());for(auto v:motionIdentities)w.n(v);for(auto&e:members)if(e.nested)e.nested->saveIdentity(w);}
 void restoreIdentity(StateReader&r){identity=uint32_t(r.n());auto count=r.n();if(count!=motionIdentities.size())throw std::runtime_error("Composite identity motion count");for(auto&v:motionIdentities)v=uint32_t(r.n());for(auto&e:members)if(e.nested)e.nested->restoreIdentity(r);}
 void warn(const std::string&s)const{if(warnings.insert(s).second)std::cerr<<"PENDING-ANIMATION "<<path<<" "<<s<<"\n";
 }
 void enqueue(RenderQueue&queue,double ms,const std::string&motionName,const Affine2D&parent,float depth,float opacity,uint32_t parentAdd,uint32_t parentMul,unsigned parentBlend=0,const Image*replacement=nullptr,const Image*messageSurface=nullptr,unsigned windowAlpha=255,const std::map<unsigned,const Image*>*uiSlots=nullptr,const AnimationContext*external=nullptr,const Matrix3D*parent3D=nullptr,unsigned filterOverride=0)const{
  if(definition.bounds[0]<0||definition.bounds[1]<0||definition.bounds[0]>4096||definition.bounds[1]>4096)throw std::runtime_error("Composite bounds "+path);
  const Motion*motion=findMotion(definition,motionName);
 if(!motion)throw std::runtime_error("Missing motion "+motionName+" in "+path);
  auto randomSeed=0x35246u^identity^motionIdentities[size_t(motion-&definition.motions[0])];
  randomSeed=((randomSeed&0xffff)*(randomSeed>>16))&1023;
  AnimationContext context{unsigned(definition.bounds[0]),unsigned(definition.bounds[1]),1280,720,randomSeed};
  if(external){context.external=external->external;context.externalMask=external->externalMask;}
  auto samples=sampleMotion(*motion,ms,1280,720,false,context);
  for(size_t index=0;index<samples.size();index++){
 auto&k=samples[index];if(filterOverride)k.textureFilter=filterOverride;
 if(parentBlend)k.flags=parentBlend;
 for(auto&w:k.unsupported)if(w!="primitive=20"&&!(w=="3d-rotation"&&(k.kind==18||(k.kind==4&&k.resource<members.size()&&members[k.resource].nested))))warn(w);
 if(!motion->layers[index].flags||!k.kind)continue;
 k.alpha=int(k.alpha)*int(opacity)/255;if(messageSurface&&k.kind==4&&k.resource==0)k.alpha=int(k.alpha)*int(windowAlpha)/255;k.addColor=colorAdd(k.addColor,parentAdd);k.mulColor=colorMultiply(k.mulColor,parentMul);
 if(k.kind==15||k.kind==16||k.kind==17){queue.control(k,depth+k.priority);continue;}
 if(k.kind==1){
  if((k.flags&0x7f)>8){warn("rectangle-blend="+std::to_string(k.flags));continue;}
  if(std::abs(k.scaleX)>=32700||std::abs(k.scaleY)>=32700){warn("rectangle-symbolic-size");continue;}
  // PE eaff0/ea798: rectangle dimensions are pixels, not image scale percentages.
  static const Image white{1,1,{255,255,255,255}};
  auto shape=imagePlacement(k.x,k.y,k.scaleX,k.scaleY,k.rotationZ,k.anchorX/100,k.anchorY/100,1,1);
  queue.image(white,affine(parent,shape),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);
  continue;
 }
 if(k.kind==10){
  if(k.resource>=members.size()||k.childMotion>=members.size())throw std::runtime_error("Mask image index "+path);
  const Image*source=members[k.resource].image.get();const Image*mask=members[k.childMotion].image.get();
   if(uiSlots){auto src=uiSlots->find(k.resource);if(src!=uiSlots->end())source=src->second;auto msk=uiSlots->find(k.childMotion);if(msk!=uiSlots->end())mask=msk->second;}
  if(!source||!mask){warn("mask-nonbitmap-resource");continue;}
   if(!source->width||!source->height||!mask->width||!mask->height)continue;
  bool rotated=false;for(auto&w:k.unsupported)if(w=="3d-rotation")rotated=true;
  if(rotated){warn("mask-3d-rotation");continue;}

  float width=k.mask.width?k.mask.width:source->width,height=k.mask.height?k.mask.height:source->height;
  float maskWidth=k.mask.maskWidth?k.mask.maskWidth:mask->width,maskHeight=k.mask.maskHeight?k.mask.maskHeight:mask->height;
   auto placement=imagePlacement(k.x,k.y,k.scaleX/100*width/source->width,k.scaleY/100*height/source->height,k.rotationZ,k.anchorX/100,k.anchorY/100,source->width,source->height);
   if(queue.supportsMasks()){queue.maskedImage(*source,*mask,k.mask,affine(parent,placement),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);continue;}
   auto&output=maskedImages[index];output.width=source->width;output.height=source->height;output.pixels.resize(source->pixels.size());
  auto pixel=[](float value,unsigned size){return unsigned(std::clamp(int(std::floor(value)),0,int(size)-1));};
  for(unsigned y=0;y<output.height;y++)for(unsigned x=0;x<output.width;x++){
   float u=(x+.5f)/output.width,v=(y+.5f)/output.height;
   unsigned sx=pixel(k.mask.sourceX-width*k.mask.alignX+u*width,source->width),sy=pixel(k.mask.sourceY-height*k.mask.alignY+v*height,source->height);
   unsigned mx=pixel(k.mask.x-maskWidth*k.mask.alignX+u*maskWidth,mask->width),my=pixel(k.mask.y-maskHeight*k.mask.alignY+v*maskHeight,mask->height);
   auto src=source->pixels.data()+(size_t(sy)*source->width+sx)*4;auto dst=output.pixels.data()+(size_t(y)*output.width+x)*4;
   std::copy(src,src+4,dst);dst[3]=gradientMaskAlpha(src[3],mask->pixels[(size_t(my)*mask->width+mx)*4],k.mask.amount,k.mask.mode);
  }

  queue.snapshot(output,affine(parent,placement),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);
  continue;
 }
 if(k.kind==20){
  if(k.resource>=members.size())throw std::runtime_error("Particle image index "+path);
  auto&e=members[k.resource];const Image*particleImage=e.image.get();bool overridden=false;
  if(uiSlots){auto slot=uiSlots->find(k.resource);if(slot!=uiSlots->end()){particleImage=slot->second;overridden=true;}}
  if(e.nested&&!overridden){
   auto frame=k;frame.scaleX=frame.scaleY=frame.scaleZ=100;
   auto base=combine(parent3D?*parent3D:from2D(parent),placement3D(frame,unsigned(k.particles.extent[0]),unsigned(k.particles.extent[1])));
   Matrix3D center;center.m[2][3]=-k.particles.extent[2]*.5f;base=combine(base,center);
   for(auto&point:particlePoints(k.particles,randomSeed,unsigned(e.nested->definition.motions.size()))){
    Matrix3D child;child.m[0][0]=child.m[1][1]=child.m[2][2]=point.scale;
    child.m[0][3]=point.x-e.nested->definition.bounds[0]*.5f;child.m[1][3]=point.y-e.nested->definition.bounds[1]*.5f;child.m[2][3]=point.z;
    child=combine(base,child);
    e.nested->enqueue(queue,nestedAnimationTime(e.nested->definition,point.milliseconds),e.nested->definition.motions.at(point.motion).name,{},depth+k.priority,k.alpha*point.alpha/255,k.addColor,k.mulColor,k.flags&0x7f,nullptr,nullptr,255,nullptr,nullptr,&child,filterOverride);
   }
   continue;
  }
  if(!particleImage){warn("particle-video pending");continue;}
   if(!particleImage->width||!particleImage->height)continue;
  bool rotation3d=false;for(auto&w:k.unsupported)if(w=="3d-rotation")rotation3d=true;
  if(rotation3d){warn("particle-3d-rotation pending");continue;}
  auto seed=0x35246u^identity^motionIdentities[size_t(motion-&definition.motions[0])];
  seed=((seed&0xffff)*(seed>>16))&1023;
  for(auto&point:particlePoints(k.particles,seed)){
   auto pos=projectParticle(point,k.particles,k.x,k.y,k.anchorX/100,k.anchorY/100,k.rotationZ,1280,720,particleImage->width,particleImage->height);
   if(!pos.visible)continue;
   auto placement=imagePlacement(pos.x,pos.y,pos.scale,pos.scale,k.rotationZ,.5f,.5f,particleImage->width,particleImage->height);
   queue.image(*particleImage,affine(parent,placement),depth+k.priority,k.alpha*point.alpha/255,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);
  }
  continue;
 }
 if(k.kind==18){
  if(k.resource>=members.size()||!members[k.resource].mesh)throw std::runtime_error("Missing model resource "+path);
  const auto&mesh=*members[k.resource].mesh;
  auto matrix=combine(parent3D?*parent3D:from2D(parent),placement3D(k,0,0));
  for(const auto&object:mesh.objects)if(object.visible)for(const auto&face:object.faces){
   if(face.material<0)continue;
   const auto&material=mesh.materials.at(unsigned(face.material));const Image*texture=nullptr;
   // PE df684 maps properties[0] to material+0xc; e0198 bypasses lighting for1.
   if(material.properties[0]!=1)warn("model-material-lighting="+std::to_string(material.properties[0]));
   auto channel=[&](unsigned index){float v;auto bits=material.properties[index];std::memcpy(&v,&bits,4);return unsigned(std::lround(std::clamp(v,0.f,1.f)*255));};
   unsigned materialAlpha=channel(2);uint32_t materialColor=(channel(3)<<16)|(channel(4)<<8)|channel(5);

   for(const auto&member:members)if(member.name==material.name){texture=member.image.get();break;}
   if(!texture){warn("model-material-texture="+material.name);continue;}
   std::array<TextureVertex,4> vertices;for(unsigned i=0;i<face.corners;i++){auto point=matrix.point(object.vertices.at(face.vertices[i]));vertices[i]={point.x,point.y,1000-point.z,face.uv[i]*texture->width,face.uv[i+4]*texture->height};}
   queue.triangle(*texture,{vertices[0],vertices[1],vertices[2]},depth+k.priority,k.alpha*materialAlpha/255,k.addColor,colorMultiply(k.mulColor,materialColor),k.flags&0x7f,k.textureFilter);
   if(face.corners==4)queue.triangle(*texture,{vertices[0],vertices[2],vertices[3]},depth+k.priority,k.alpha*materialAlpha/255,k.addColor,colorMultiply(k.mulColor,materialColor),k.flags&0x7f,k.textureFilter);
  }
  continue;
 }
 if(k.kind!=4)continue;
 if(k.resource>=members.size())throw std::runtime_error("Composite image index "+path);
   auto&e=members[k.resource];
 const Image*slot=k.resource==0?replacement:k.resource==1?messageSurface:nullptr;
 if(uiSlots){auto override=uiSlots->find(k.resource);if(override!=uiSlots->end())slot=override->second;}
 const Image*source=slot?slot:e.image.get();bool dynamic=false;
   if(slot){
    queue.image(*source,affine(parent,imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,source->width,source->height)),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);continue;
   }
   if(e.nested){
 if(!k.childTimeResolved){warn("nested-time context unresolved");continue;}
 if(k.childMotion>=e.nested->definition.motions.size())throw std::runtime_error("Nested motion index "+path);
 if(parent3D||k.rotationX||k.rotationY||k.z){
  auto matrix=combine(parent3D?*parent3D:from2D(parent),placement3D(k,unsigned(e.nested->definition.bounds[0]),unsigned(e.nested->definition.bounds[1])));
  e.nested->enqueue(queue,nestedAnimationTime(e.nested->definition,k.childTime),e.nested->definition.motions[k.childMotion].name,{},depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,nullptr,nullptr,255,nullptr,nullptr,&matrix,filterOverride);continue;
 }
 auto local=imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,unsigned(e.nested->definition.bounds[0]),unsigned(e.nested->definition.bounds[1]));
 e.nested->enqueue(queue,nestedAnimationTime(e.nested->definition,k.childTime),e.nested->definition.motions[k.childMotion].name,affine(parent,local),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,nullptr,nullptr,255,nullptr,nullptr,nullptr,filterOverride);
 continue;
 }
   else if(!e.video.empty()){
    auto&clip=clips[index];
 if(!clip||&clip->bytes!=&e.video){clip=std::make_unique<EmbeddedVideo>(e.video);

 }
 source=&clip->at(k.mediaTime);if(clip->audible)warn("video nonzero audio requires mixer binding");dynamic=true;
   }
   if(source&&parent3D){auto matrix=combine(*parent3D,placement3D(k,source->width,source->height));quad(queue,dynamic?queue.snapshotImage(*source):*source,matrix,k,depth+k.priority);continue;}
   if(source){auto place=affine(parent,imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,source->width,source->height));
 if(dynamic)queue.snapshot(*source,place,depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);
 else queue.image(*source,place,depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);
 }
  }
 }
};
SceneImages::SceneImages(qlie::Assets&a):assets(a){for(auto type:{"image","chara","face","message"}){std::istringstream in(qlie::cp932(a.read(std::string("avg/imoscripter_template_")+type+".txt")));
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
void SceneImages::loadReplacement(Layer&layer,const std::string&path){
 layer.params["@replacement"]=path;
 if(path.empty()){layer.scriptReplacement=std::make_shared<Image>();return;}
 auto at=cache.find(path);if(at==cache.end())at=cache.emplace(path,std::make_shared<Image>(image(assets.read(path)))).first;
 layer.scriptReplacement=at->second;
}
void SceneImages::uiSlot(const std::string&name,unsigned slot,const std::string&path){if(name.rfind("ui_",0)!=0)throw std::runtime_error("UI slot owner");auto i=cache.find(path);if(i==cache.end())i=cache.emplace(path,std::make_shared<Image>(image(assets.read(path)))).first;layers.at(name).uiSlots[slot]=i->second;}
void SceneImages::uiSlot(const std::string&name,unsigned slot,const Image&value){if(name.rfind("ui_",0)!=0)throw std::runtime_error("UI slot owner");layers.at(name).uiSlots[slot]=std::make_shared<Image>(value);}
bool SceneImages::uiHit(const std::string&name,float x,float y)const{
 if(name.rfind("ui_",0)!=0)throw std::runtime_error("UI hit owner");
 auto found=layers.find(name);if(found==layers.end())return false;const auto&l=found->second;
 if(!l.composite||l.params.at("show")=="false")return false;
 const auto&definition=l.composite->definition;auto selected=l.params.find("motion");auto motion=findMotion(definition,selected==l.params.end()?"":selected->second);if(!motion)return false;
 auto align=l.params.at("align");float ax=align.rfind("center",0)==0?.5f:align.rfind("right",0)==0?1.f:0.f,ay=align.find(":bottom")!=align.npos?1.f:align.find(":center")!=align.npos?.5f:0.f;
 auto parent=imagePlacement(value(l,"x",0),value(l,"y",0),value(l,"scalex",100)/100,value(l,"scaley",100)/100,value(l,"rotz",0),ax,ay,unsigned(definition.bounds[0]),unsigned(definition.bounds[1]));
 auto samples=sampleMotion(*motion,l.milliseconds,1280,720,true,AnimationContext{unsigned(definition.bounds[0]),unsigned(definition.bounds[1]),1280,720});
 for(size_t i=0;i<samples.size();i++){auto&k=samples[i];if(!motion->layers[i].flags||k.kind!=6)continue;
  // Primitive6 is the authored collision rectangle; eae50 is its debug overlay.
  auto local=imagePlacement(k.x,k.y,k.scaleX,k.scaleY,k.rotationZ,k.anchorX/100,k.anchorY/100,1,1);
  auto matrix=affine(k.screen,affine(parent,local));float det=matrix.a*matrix.d-matrix.b*matrix.c;if(std::abs(det)<1e-9f)continue;
  float dx=x-matrix.x,dy=y-matrix.y,u=(matrix.d*dx-matrix.b*dy)/det,v=(-matrix.c*dx+matrix.a*dy)/det;
  if(u>=0&&u<=1&&v>=0&&v<=1)return true;
 }
 return false;
}
void SceneImages::uiExternal(const std::string&name,unsigned index,float v){if(name.rfind("ui_",0)!=0||index>=16||!std::isfinite(v))throw std::runtime_error("UI external variable");layers.at(name).params["@exv"+std::to_string(index)]=std::to_string(v);}
void SceneImages::clear(){layers.clear();
 cache.clear();
 composites.clear();
 }
std::vector<SceneSoundEvent> SceneImages::takeSoundEvents(){
 std::vector<SceneSoundEvent> result;
 for(auto&entry:layers){auto&layer=entry.second;
  int base=layer.params.count("dchannel")?std::stoi(layer.params.at("dchannel")):0;
  if(base<0||base>=16)throw std::runtime_error("Direct sound channel out of range");
  unsigned category=entry.first.rfind("chara",0)==0?1:2;
  auto type=layer.params.find("voltype");if(type!=layer.params.end()){if(type->second=="bgm")category=0;else if(type->second=="voice")category=1;else if(type->second=="se")category=2;}
  int volume=layer.params.count("vol")?int(std::stof(layer.params.at("vol"))*256/100):256;
  if(layer.audioStopPending){SceneSoundEvent sound;sound.stop=true;sound.channel=8+unsigned(base);result.push_back(sound);layer.audioStopPending=false;}
  if(!layer.composite)continue;
  auto&object=*layer.composite;auto i=layer.params.find("motion");
  const Motion*motion=findMotion(object.definition,i==layer.params.end()?"":i->second);
  if(!motion)continue;
  for(auto event:animationSoundEvents(*motion,layer.audioMilliseconds,layer.milliseconds)){
   if(unsigned(base)+event.channel>=16)throw std::runtime_error("Embedded sound channel out of range");
   SceneSoundEvent sound; sound.category=category;sound.stop=event.stop;sound.loop=event.loop;sound.channel=8+unsigned(base)+event.channel;sound.gain=std::clamp(int(event.gain*256)*volume/256,0,256)/256.f;
   if(!event.stop&&sound.gain==0)continue;
   if(!event.stop){if(event.resource>=object.sounds.size()||object.sounds[event.resource].empty())throw std::runtime_error("Embedded sound resource missing "+layer.path);
    sound.source="@abmp:"+std::to_string(event.resource)+":"+layer.path;sound.bytes=object.sounds[event.resource];
   }result.push_back(std::move(sound));
  }layer.audioMilliseconds=layer.milliseconds;
 }return result;
}
float SceneImages::value(const Layer&layer,const std::string&key,float fallback){
 auto tween=layer.tweens.find(key);
 if(tween==layer.tweens.end()){auto param=layer.params.find(key);if(param==layer.params.end())return fallback;
 if(key=="texfilter"){if(param->second=="pass")return 0;if(param->second=="point")return 1;if(param->second=="linear")return 2;}
 return numeric(param->second);}
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
 if(layer.params.count("@hiding")&&layer.params.at("@hiding")=="true"&&layer.composite){auto*motion=findMotion(layer.composite->definition,"*Disable");if(motion){auto samples=sampleMotion(*motion,layer.milliseconds);if(std::none_of(samples.begin(),samples.end(),[](auto&sample){return sample.kind!=0;})){layer.params["show"]="false";layer.params.erase("@hiding");}}}
 }}
bool SceneImages::command(const std::string&line){auto p=line.find(44);
 auto cmd=line.substr(1,p-1);
 bool chara=cmd.rfind("chara",0)==0,face=cmd=="face",message=cmd=="message",ui=cmd.rfind("ui_",0)==0;
 if(!ui&&!chara&&!face&&!message&&cmd.rfind("bg",0)!=0&&cmd!="effect"&&cmd!="env")return false;
 auto&l=layers[cmd];
 const std::map<std::string,std::string> faceDefaults{{"mode","auto"},{"disp","once"},{"show","false"},{"x","0"},{"y","0"},{"scalex","100"},{"scaley","100"},{"alpha","255"},{"pri","0"},{"align","left:top"},{"@face-active","false"}};
 if(face&&l.params.empty())l.params=faceDefaults;
 if(message&&l.params.empty())l.params={{"mode","cls"},{"show","false"},{"x","0"},{"y","720"},{"scalex","100"},{"scaley","100"},{"alpha","255"},{"pri","1100"},{"align","left:top"},{"textarea","920:300"},{"fontsize","26:26"},{"fontcolor","$FFFFFF"}};
 if(l.params.empty())l.params={{"x",ui?"0":"640"},{"y",ui?"0":chara?"720":"360"},{"scalex","100"},{"scaley","100"},{"alpha","255"},{"pri",chara?"500":"0"},{"show","true"},{"align",chara?"center:bottom":(ui||cmd=="effect")?"left:top":"center:center"}};
 if(chara){auto motion=l.params.find("motion");if(motion!=l.params.end()&&motion->second!="def"&&motion->second!="0"&&motion->second.find('+')==std::string::npos){motion->second="def";l.milliseconds=0;l.audioMilliseconds=-1;}}
 auto args=split(expand(p==line.npos?"":line.substr(p+1),chara?"chara":face?"face":message?"message":"image"),44);
 Layer before=l;bool file=false,baseChanged=false,position=false,color=false,messageShow=false;bool replacementChanged[4]={};
 if(std::any_of(args.begin(),args.end(),[](const std::string&arg){return arg.rfind("file:",0)==0;})){
  l.scriptReplacement.reset();l.params.erase("@replacement");for(unsigned i=0;i<4;i++)l.params.erase("rep"+std::to_string(i));
 }
 for(auto&s:args){if(s.empty())continue;
 auto c=s.find(58);
 if(c==s.npos)throw std::runtime_error("Unknown image template "+s);
 auto key=s.substr(0,c),v=s.substr(c+1);
 if(key.size()==4&&key.rfind("rep",0)==0&&key[3]>='0'&&key[3]<='3')replacementChanged[key[3]-'0']=true;
 if(face&&key=="clear"&&v=="def"){l=Layer{};l.params=faceDefaults;continue;}
 if(key=="clear"){l.params.erase("@portrait");l.params.erase("@replacement");l.scriptReplacement.reset();l.path.clear();
 l.pixels.reset();
 l.composite.reset();
 l.params["show"]="false";
 continue;
 }l.params[key]=v;
 if(message&&key=="show")messageShow=true;
 if(key=="file"){file=true;if(face)l.params["@face-active"]="true";}
 if((chara||face)&&key=="base")baseChanged=true;
 if(key=="x"||key=="y"||key=="ax"||key=="ay"||key=="scalex"||key=="scaley"||key=="rotz"||key=="pri")position=true;
 if(key=="alpha"||key=="mulcolor"||key=="addcolor")color=true;
 if(key=="vol"&&std::stof(v)==0)l.audioStopPending=true;
 if(key=="motion"){l.milliseconds=0;l.audioMilliseconds=-1;}
 if(key!="rep0"&&key!="rep1"&&key!="rep2"&&key!="rep3"&&key!="textarea"&&key!="fontsize"&&key!="fontcolor"&&key!="fontno"&&key!="trans"&&key!="id"&&key!="mode"&&key!="disp"&&key!="base"&&key!="time"&&key!="speed"&&key!="move"&&key!="movepow"&&key!="ax"&&key!="ay"&&key!="vol"&&key!="voltype"&&key!="dchannel"&&key!="motion"&&key!="file"&&key!="x"&&key!="y"&&key!="scalex"&&key!="scaley"&&key!="alpha"&&key!="show"&&key!="pri"&&key!="align"&&key!="texfilter"&&key!="rotz"&&key!="mulcolor"&&key!="addcolor")std::cerr<<"PENDING-IMAGE-PROPERTY "<<cmd<<" "<<key<<"="<<v<<"\n";
 }
 if(messageShow&&l.composite){const std::string mode=l.params["show"]=="true"?"*Enable":"*Disable";
  if(findMotion(l.composite->definition,mode)){l.params["motion"]=mode;l.milliseconds=0;l.audioMilliseconds=-1;
   if(mode=="*Disable"){l.params["show"]="true";l.params["@hiding"]="true";}else l.params.erase("@hiding");
  }
 }
 // Imoscripter applies rep0..rep3 in order, each to member zero.
 for(unsigned i=0;i<4;i++)if(replacementChanged[i]){
  auto replacement=l.params.at("rep"+std::to_string(i));std::string path;
  if(!replacement.empty()&&replacement!="none"){
   path="image/"+replacement;
   if(!assets.exists(path)){if(assets.exists(path+".png"))path+=".png";else if(assets.exists(path+".b"))path+=".b";else throw std::runtime_error("Missing replacement image "+path);}
  }loadReplacement(l,path);
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
 if(f=="none"||f=="none:none"){l.params.erase("@portrait");l.params.erase("@replacement");l.scriptReplacement.reset();l.path.clear();
 l.pixels.reset();
 l.composite.reset();
 return true;
 }if(chara){auto pair=split(f,58);
 if(pair.size()>1)l.prefix=pair[1];
 f=pair[0].find(47)!=std::string::npos?pair[0]:l.prefix+pair[0];
 }std::string path=(ui?"":chara?"chara/":face?"face/":message?"avg/window/":"image/")+f;
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
std::pair<unsigned,unsigned> SceneImages::dimensions(const std::string&name)const{const auto&layer=layers.at(name);if(layer.composite)return {unsigned(layer.composite->definition.bounds[0]),unsigned(layer.composite->definition.bounds[1])};if(layer.pixels)return {layer.pixels->width,layer.pixels->height};throw std::runtime_error("Image dimensions unavailable: "+name);}
void SceneImages::draw(qlie::Bytes&out)const{render(out,0);}
void SceneImages::drawScene(qlie::Bytes&out)const{render(out,1);}
void SceneImages::drawFaces(qlie::Bytes&out)const{render(out,2);}
void SceneImages::drawMessages(qlie::Bytes&out)const{render(out,3);}
MessageStyle SceneImages::messageStyle()const{MessageStyle style;auto it=layers.find("message");if(it==layers.end())return style;auto&params=it->second.params;
 auto pair=[&](const std::string&key,unsigned&a,unsigned&b){auto found=params.find(key);if(found==params.end())return;auto values=split(found->second,':');a=unsigned(numeric(values[0]));b=values.size()>1?unsigned(numeric(values[1])):a;};
 pair("textarea",style.width,style.height);pair("fontsize",style.fontWidth,style.fontHeight);auto color=params.find("fontcolor");if(color!=params.end())style.color=uint32_t(numeric(color->second));return style;
}
bool SceneImages::messageVisible()const{auto it=layers.find("message");return it!=layers.end()&&it->second.params.at("show")=="true";}
void SceneImages::setMessageImage(const Image&image,bool show){if(!layers.count("message"))command("^message,$window01");auto&layer=layers.at("message");layer.pixels=std::make_shared<Image>(image);
 if(show&&(layer.params["show"]!="true"||layer.params.count("@hiding"))){if(layer.composite&&findMotion(layer.composite->definition,"*Enable")){layer.params["motion"]="*Enable";layer.milliseconds=0;layer.audioMilliseconds=-1;}layer.params["show"]="true";layer.params.erase("@hiding");}
}

void SceneImages::draw(RenderTarget&target)const{qlie::Bytes unused;render(unused,0,&target);}
void SceneImages::drawScene(RenderTarget&target)const{qlie::Bytes unused;render(unused,1,&target);}
void SceneImages::drawFaces(RenderTarget&target)const{qlie::Bytes unused;render(unused,2,&target);}
void SceneImages::drawMessages(RenderTarget&target)const{qlie::Bytes unused;render(unused,3,&target);}
void SceneImages::render(qlie::Bytes&out,unsigned pass,RenderTarget*target)const{
 if(!target&&out.size()!=1280*720*4)throw std::runtime_error("Scene canvas size");
 std::vector<const Layer*>sorted;
 for(auto&x:layers){auto&layer=x.second;bool face=x.first=="face",message=x.first=="message";
 if((pass==1&&(face||message))||(pass==2&&!face)||(pass==3&&!message))continue;
 if(face&&layer.params.at("disp")!="keep"&&layer.params.at("@face-active")!="true"){
 if(layer.params.at("show")=="true"&&layer.params.at("mode")=="auto"&&!faceWarning){std::cerr<<"PENDING-FACE automatic portrait selection\n";faceWarning=true;}continue;
 }
 auto base=layer.params.find("base");
 if(base!=layer.params.end()&&!base->second.empty()&&base->second!="none"&&!layer.params.count("@portrait"))continue;
 if((layer.pixels||layer.composite)&&layer.params.at("show")!="false")sorted.push_back(&layer);
 }
 auto get=[](const Layer*l,const std::string&k,float def){return value(*l,k,def);};
 std::stable_sort(sorted.begin(),sorted.end(),[&](auto*a,auto*b){return get(a,"pri",0)<get(b,"pri",0);});
 RenderQueue queue(target&&target->supportsMasks());
 for(auto*l:sorted){
 auto beforeImages=queue.imageCount();
 auto align=l->params.at("align");
 float ax=align.rfind("center",0)==0?0.5f:align.rfind("right",0)==0?1.f:0.f,ay=align.find(":bottom")!=align.npos?1.f:align.find(":center")!=align.npos?0.5f:0.f;
 unsigned w=l->composite?unsigned(l->composite->definition.bounds[0]):l->pixels->width,h=l->composite?unsigned(l->composite->definition.bounds[1]):l->pixels->height;
 auto placement=imagePlacement(get(l,"x",640),get(l,"y",360),get(l,"scalex",100)/100,get(l,"scaley",100)/100,get(l,"rotz",0),ax,ay,w,h);
 std::map<unsigned,const Image*> uiSlots;for(auto&slot:l->uiSlots)uiSlots[slot.first]=slot.second.get();if(l->scriptReplacement)uiSlots[0]=l->scriptReplacement.get();
 AnimationContext external;for(unsigned i=0;i<16;i++){auto v=l->params.find("@exv"+std::to_string(i));if(v!=l->params.end()){external.external[i]=numeric(v->second);external.externalMask|=uint16_t(1u<<i);}}
 if(l->composite){auto m=l->params.find("motion");l->composite->enqueue(queue,l->milliseconds,m==l->params.end()?"":m->second,placement,get(l,"pri",0),get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)),0,l->params.count("@portrait")?l->pixels.get():nullptr,layers.count("message")&&l==(&layers.at("message"))?l->pixels.get():nullptr,windowAlpha,uiSlots.empty()?nullptr:&uiSlots,&external,nullptr,unsigned(get(l,"texfilter",0)));}
 else queue.image(*l->pixels,placement,get(l,"pri",0),get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)),0,unsigned(get(l,"texfilter",0)));
 if(queue.imageCount()>beforeImages){if(qlie::canonical(l->path).rfind("image/ev/",0)==0)shownFiles.insert(qlie::canonical(l->path));auto replacement=l->params.find("@replacement");if(replacement!=l->params.end()&&qlie::canonical(replacement->second).rfind("image/ev/",0)==0)shownFiles.insert(qlie::canonical(replacement->second));}
 }
 if(target)queue.draw(*target,1280,720);else queue.draw(out,1280,720);
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
 if(l.params.count("@replacement"))loadReplacement(l,l.params.at("@replacement"));
 next[name]=std::move(l);
 }r.end();
 layers=std::move(next);
 particleState=savedParticleState;
 }

}
