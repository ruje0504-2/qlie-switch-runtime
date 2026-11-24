// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.h"
#include "scene_images.hpp"
#include "video_surface_image.hpp"
#include "asset_read_ahead.hpp"
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
#include <chrono>
#include <cstdio>
#include <iostream>
#include <cmath>
#include <cstring>
namespace whj2 {
namespace {
float numeric(const std::string&s){return !s.empty()&&s[0]=='$'?float(std::stoul(s.substr(1),nullptr,16)):std::stof(s);
 }
std::string characterImageName(const std::string&name,std::string&prefix){
 auto parts=split(name,':');
 if(parts.size()>1)prefix=parts[1];
 return parts[0].find('/')==std::string::npos?prefix+parts[0]:parts[0];
}

// Scene asset and UI-slot owners replace the entire image on change.
std::shared_ptr<const Image> sceneBitmap(Image pixels){
 pixels.version.publish();
 return std::make_shared<const Image>(std::move(pixels));
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
 std::shared_ptr<const qlie::Bytes> bytes;
 std::string source;
 KVideo*video=nullptr;
 std::shared_ptr<Image> frame;
 std::shared_ptr<const Image> heldFrame;
 uint64_t heldTick=0;
 uint64_t ticks=0;
 bool ended=false,audible=false,softwareOnly=false;
 explicit EmbeddedVideo(std::shared_ptr<const qlie::Bytes> b,std::string name={})
  :bytes(std::move(b)),source(std::move(name)){reset();}
 ~EmbeddedVideo(){if(video)kvideo_close(video);}
 void reset(){
  if(video)kvideo_close(video);
  video=softwareOnly?kvideo_open_mode(bytes->data(),bytes->size(),KVIDEO_SOFTWARE):kvideo_open(bytes->data(),bytes->size());
  if(!video)throw std::runtime_error("Embedded video open failed: "+source);
  const unsigned width=kvideo_source_width(video),height=kvideo_source_height(video);
  frame=std::make_shared<Image>(Image{width,height,qlie::Bytes(size_t(width)*height*4)});
  frame->version.publish();
  ticks=0;ended=false;audible=false;
 }
 std::shared_ptr<const Image> at(double ms,bool live=false,bool nativeVideo=false){
  const uint64_t target=uint64_t(std::max(0.,ms)*60/1000);
  if(heldFrame&&target<heldTick)heldFrame.reset();
  bool rewound=false;
  if(target+1<ticks){
   if(kvideo_rewind(video)<0)reset();
   else{ticks=0;ended=false;audible=false;rewound=true;}
  }
  if(ended||ticks>target)return heldFrame?heldFrame:frame;
  if(nativeVideo&&!softwareOnly)kvideo_enable_surface(video);
  // A long stall can move the script clock ahead by seconds. Work through
  // that backlog in small batches on the interactive presentation path.
  const uint64_t due=live?std::min(target,ticks+5):target;
  const bool borrowed=frame.use_count()>1;
  auto output=borrowed?std::make_shared<Image>(Image{frame->width,frame->height,qlie::Bytes(frame->pixels.size())}):frame;
  KImage dst{0,0,output->width,output->height,size_t(output->width)*4,output->pixels.data()};
  uint8_t*pcm=nullptr;size_t count=0;
  auto before=kvideo_written_frames(video);
  int code=kvideo_advance(video,due,&dst,&pcm,&count);
  if(pcm&&count&&std::any_of(pcm,pcm+count,[](uint8_t sample){return sample!=0;}))audible=true;
  free(pcm);
  if(code<0){
   KVideoStats stats{};
   kvideo_stats(video,&stats);
   if(!softwareOnly&&(stats.hardware_pending||stats.hardware_frames)){
    QLIE_LOGF("VIDEO recovery source=%s tick=%llu switching=software reason=%s\n",
                 source.c_str(),static_cast<unsigned long long>(target),kvideo_error(video));
    if(before&&ticks){heldFrame=frame;heldTick=ticks-1;}
    softwareOnly=true;
    reset();
    return at(ms,live,nativeVideo);
   }
   if(rewound){reset();return at(ms,live,nativeVideo);}
   throw std::runtime_error("Embedded video "+source+": "+kvideo_error(video));
  }
  if(kvideo_written_frames(video)!=before){
   output->version.publish();
   KVideoSurface surface{};
   if(kvideo_get_surface(video,&surface)){
    auto retained=std::make_shared<VideoSurfaceImage>();
    retained->frame={kvideo_clone_frame(surface.frame),kvideo_free_frame};
    if(!retained->frame)throw std::runtime_error("Embedded video frame reference failed: "+source);
    surface.frame=retained->frame.get();
    surface.version=output->version.get();
    retained->surface=surface;
    retained->source=source;
    retained->readFrame=kvideo_read_frame;
    output->videoSurface=std::move(retained);
   }else output->videoSurface.reset();
   frame=std::move(output);
  }
  else if(rewound){
   std::fill(output->pixels.begin(),output->pixels.end(),0);
   output->videoSurface.reset();
   output->version.publish();frame=std::move(output);
  }
  ticks=due+1;ended=code==1;
  if(heldFrame&&(due>=heldTick||ended))heldFrame.reset();
  return heldFrame?heldFrame:frame;
 }
};
struct PreparedEmbeddedOpening {
 struct Source {size_t layer;std::shared_ptr<const qlie::Bytes> bytes;std::string name;};
 std::map<size_t,std::unique_ptr<EmbeddedVideo>> clips;
 PreparedEmbeddedOpening(const std::vector<Source>&sources,bool nativeVideo){
  for(const auto&source:sources){
   auto clip=std::make_unique<EmbeddedVideo>(source.bytes,source.name);
   clip->at(0,false,nativeVideo);
   clips.emplace(source.layer,std::move(clip));
  }
 }
 size_t bytesHeld()const{
  size_t size=0;
  for(const auto&clip:clips)size+=clip.second->frame->pixels.size();
  return size;
 }
};
namespace {
constexpr auto eyecatchPath="image/cut/eyecatch01.b";
}
struct Composite {
 struct Element {std::shared_ptr<const Image>image;std::shared_ptr<Model>mesh;std::string name;
 std::shared_ptr<Composite>nested;
 std::shared_ptr<const qlie::Bytes> video;
 };
 Animation definition;
 bool hasVideo=false;
 uint32_t identity=0;std::vector<uint32_t>motionIdentities;
 std::vector<Element>members;
 qlie::Bytes eyecatchFallback;
 std::vector<qlie::Bytes>sounds;
 std::string path;
 mutable std::set<std::string>warnings;
 mutable std::map<size_t,std::unique_ptr<EmbeddedVideo>>clips;
 mutable std::map<size_t,Image>maskedImages;
 explicit Composite(const qlie::Bytes&bytes,const std::string&name,uint32_t&randomState,bool deferred=false):path(name){
  auto nextId=[&]{return deferred?randomState++:particleRandomNext(randomState);};
  identity=nextId();
  bool metadata=false;
  for(auto&m:abmp(bytes)){
   if(m.type.rfind("abdata",0)==0){definition=animation(m.type,m.data);
 for(size_t i=0;i<definition.motions.size();i++)motionIdentities.push_back(nextId());
 metadata=true;
 }
   else if(m.type.rfind("abimg",0)==0){Element e;e.name=m.name;
 if(!m.data.empty()){
    if(m.data.size()>=5&&std::equal(m.data.begin(),m.data.begin()+5,"abmp1"))e.nested=std::make_shared<Composite>(m.data,name+"#"+m.name,randomState,deferred);
    else if(kvideo_is_container(m.data.data(),m.data.size())){e.video=std::make_shared<const qlie::Bytes>(std::move(m.data));
 }
    else if(m.data.size()>=12&&std::equal(m.data.begin(),m.data.begin()+12,"ARGBModel1.0"))e.mesh=std::make_shared<Model>(model(m.data));
    // The story cards supply a portrait for slot zero. Keep the embedded
    // bitmap compressed until a caller draws the card without a replacement.
    else if(path==eyecatchPath&&members.empty())eyecatchFallback=std::move(m.data);
    else e.image=sceneBitmap(image(m.data));
   }
   hasVideo=hasVideo||bool(e.video)||(e.nested&&e.nested->hasVideo);
   members.push_back(std::move(e));
 }
   else if(m.type.rfind("absnd",0)==0)sounds.push_back(std::move(m.data));
  }
  if(!metadata)throw std::runtime_error("Composite lacks animation metadata "+name);
  for(auto&motion:definition.motions)for(auto&layer:motion.layers){if(layer.mode>2)warn("layer-mode="+std::to_string(layer.mode));
 for(auto&k:layer.keys)if(k[4]!=0&&k[4]!=1&&k[4]!=4&&k[4]!=6&&k[4]!=10&&k[4]!=15&&k[4]!=16&&k[4]!=17&&k[4]!=18&&k[4]!=20)warn("primitive="+std::to_string(k[4]));
 }
 }
 size_t closeVideoClips(AssetReadAhead*worker){
  if(!hasVideo)return 0;
  size_t count=clips.size();
  // Each clip owns its compressed input, independent of the cached composite.
  // The renderer holds its own reference to any frame still submitted to the GPU.
  if(worker)for(auto&entry:clips)worker->retire(std::move(entry.second));
  clips.clear();
  for(auto&member:members)if(member.nested)count+=member.nested->closeVideoClips(worker);
  return count;
 }
 void resolveIdentities(const std::vector<uint32_t>&values){
  identity=values.at(identity);
  for(auto&id:motionIdentities)id=values.at(id);
  for(auto&e:members)if(e.nested)e.nested->resolveIdentities(values);
 }
 void loadEyecatchFallback(){
  if(eyecatchFallback.empty())return;
  members.front().image=sceneBitmap(image(eyecatchFallback));
  qlie::Bytes{}.swap(eyecatchFallback);
 }
 size_t bytesHeld()const{
  size_t size=eyecatchFallback.size();
  for(const auto&e:members){if(e.image)size+=e.image->pixels.size();if(e.nested)size+=e.nested->bytesHeld();if(e.video)size+=e.video->size();}
  for(const auto&sound:sounds)size+=sound.size();
  for(const auto&clip:clips)size+=clip.second->frame->pixels.size();
  return size;
 }
 std::vector<PreparedEmbeddedOpening::Source> openingVideoSources()const{
  std::vector<PreparedEmbeddedOpening::Source> sources;
  if(definition.motions.empty())return sources;
  const auto&motion=definition.motions.front();
  const auto keys=sampleMotion(motion,0,1280,720,false);
  for(size_t layer=0;layer<keys.size();++layer){
   const auto&key=keys[layer];
   if(!motion.layers[layer].flags||key.kind!=4||key.alpha<=0||key.resource>=members.size())continue;
   const auto&element=members[key.resource];
   if(element.video)sources.push_back({layer,element.video,path+"#"+element.name});
  }
  return sources;
 }
 void prepareOpeningVideo(bool nativeVideo){
  clips.merge(PreparedEmbeddedOpening(openingVideoSources(),nativeVideo).clips);
 }
 void saveIdentity(StateWriter&w)const{w.n(identity);w.n(motionIdentities.size());for(auto v:motionIdentities)w.n(v);for(auto&e:members)if(e.nested)e.nested->saveIdentity(w);}
 void restoreIdentity(StateReader&r){identity=uint32_t(r.n());auto count=r.n();if(count!=motionIdentities.size())throw std::runtime_error("Composite identity motion count");for(auto&v:motionIdentities)v=uint32_t(r.n());for(auto&e:members)if(e.nested)e.nested->restoreIdentity(r);}
 void warn(const std::string&s)const{if(qlie::diagnostics&&warnings.insert(s).second)QLIE_LOG_LINE(<<"PENDING-ANIMATION "<<path<<" "<<s<<"\n");
 }
 void enqueue(RenderQueue&queue,double ms,const std::string&motionName,const Affine2D&parent,float depth,float opacity,uint32_t parentAdd,uint32_t parentMul,unsigned parentBlend=0,const Image*replacement=nullptr,const Image*messageSurface=nullptr,unsigned windowAlpha=255,const std::map<unsigned,const Image*>*uiSlots=nullptr,const AnimationContext*external=nullptr,const Matrix3D*parent3D=nullptr,unsigned filterOverride=0,bool liveVideo=false)const{
  if(definition.bounds[0]<0||definition.bounds[1]<0||definition.bounds[0]>4096||definition.bounds[1]>4096)throw std::runtime_error("Composite bounds "+path);
  const Motion*motion=findMotion(definition,motionName);
 if(!motion)throw std::runtime_error("Missing motion "+motionName+" in "+path);
  auto randomSeed=0x35246u^identity^motionIdentities[size_t(motion-&definition.motions[0])];
  randomSeed=((randomSeed&0xffff)*(randomSeed>>16))&1023;
  AnimationContext context{unsigned(definition.bounds[0]),unsigned(definition.bounds[1]),1280,720,randomSeed};
  if(external){context.external=external->external;context.externalMask=external->externalMask;context.randomCursor=external->randomCursor;}
  auto samples=sampleMotion(*motion,ms,1280,720,false,context);
  for(size_t index=0;index<samples.size();index++){
 auto&k=samples[index];if(filterOverride)k.textureFilter=filterOverride;
 if(parentBlend)k.flags=parentBlend;
 for(auto&w:k.unsupported)if(w!="primitive=20"&&!(w=="3d-rotation"&&(k.kind==18||k.kind==4)))warn(w);
 if(!motion->layers[index].flags||!k.kind)continue;
 k.alpha=int(k.alpha)*int(opacity)/255;if(messageSurface&&k.kind==4&&k.resource==0)k.alpha=int(k.alpha)*int(windowAlpha)/255;k.addColor=colorAdd(k.addColor,parentAdd);k.mulColor=colorMultiply(k.mulColor,parentMul);
 if(k.kind==15||k.kind==16||k.kind==17){queue.control(k,depth+k.priority);continue;}
 if(k.kind==1){
  if((k.flags&0x7f)>8){warn("rectangle-blend="+std::to_string(k.flags));continue;}
  if(std::abs(k.scaleX)>=32700||std::abs(k.scaleY)>=32700){warn("rectangle-symbolic-size");continue;}
  // PE eaff0/ea798: rectangle dimensions are pixels, not image scale percentages.
  static const Image white=[]{Image pixel{1,1,{255,255,255,255}};pixel.version.publish();return pixel;}();
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
    e.nested->enqueue(queue,nestedAnimationTime(e.nested->definition,point.milliseconds),e.nested->definition.motions.at(point.motion).name,{},depth+k.priority,k.alpha*point.alpha/255,k.addColor,k.mulColor,k.flags&0x7f,nullptr,nullptr,255,nullptr,nullptr,&child,filterOverride,liveVideo);
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
 const Image*source=slot?slot:e.image.get();
   if(slot){
    if(parent3D||k.rotationX||k.rotationY||k.z){auto matrix=combine(parent3D?*parent3D:from2D(parent),placement3D(k,source->width,source->height));quad(queue,*source,matrix,k,depth+k.priority);continue;}
    queue.image(*source,affine(parent,imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,source->width,source->height)),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);continue;
   }
   if(e.nested){
 if(!k.childTimeResolved){warn("nested-time context unresolved");continue;}
 if(k.childMotion>=e.nested->definition.motions.size())throw std::runtime_error("Nested motion index "+path);
 AnimationContext nestedContext;nestedContext.randomCursor=context.randomCursor;
 if(parent3D||k.rotationX||k.rotationY||k.z){
  auto matrix=combine(parent3D?*parent3D:from2D(parent),placement3D(k,unsigned(e.nested->definition.bounds[0]),unsigned(e.nested->definition.bounds[1])));
  e.nested->enqueue(queue,nestedAnimationTime(e.nested->definition,k.childTime),e.nested->definition.motions[k.childMotion].name,{},depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,nullptr,nullptr,255,nullptr,&nestedContext,&matrix,filterOverride,liveVideo);continue;
 }
 auto local=imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,unsigned(e.nested->definition.bounds[0]),unsigned(e.nested->definition.bounds[1]));
 e.nested->enqueue(queue,nestedAnimationTime(e.nested->definition,k.childTime),e.nested->definition.motions[k.childMotion].name,affine(parent,local),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,nullptr,nullptr,255,nullptr,&nestedContext,nullptr,filterOverride,liveVideo);
 continue;
 }
   else if(e.video){
    if(k.alpha<=0)continue;
    auto&clip=clips[index];
    if(!clip||clip->bytes!=e.video)clip=std::make_unique<EmbeddedVideo>(e.video,path+"#"+e.name);
    source=&queue.retainImage(clip->at(k.mediaTime,liveVideo,queue.supportsVideoSurfaces()));
    if(clip->audible)warn("video nonzero audio requires mixer binding");
   }
   if(source&&(parent3D||k.rotationX||k.rotationY||k.z)){auto matrix=combine(parent3D?*parent3D:from2D(parent),placement3D(k,source->width,source->height));quad(queue,*source,matrix,k,depth+k.priority);continue;}
   if(source){auto place=affine(parent,imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,source->width,source->height));
 queue.image(*source,place,depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);
 }
  }
 }
};
std::shared_ptr<const Image> SceneImages::loadBitmap(const std::string&path){
 if(readAhead)if(auto ready=readAhead->take(qlie::canonical(path)))return std::move(ready->bitmap);
 return sceneBitmap(image(assets.read(path)));
}
std::shared_ptr<Composite> SceneImages::sceneComposite(const std::string&path){
 if(const auto cached=composites.find(path);cached!=composites.end()){
  auto&object=cached->second;
  if(readAhead&&object.use_count()==1&&object->hasVideo&&object->clips.empty())
   if(auto ready=readAhead->takeReady(qlie::canonical(path));ready&&ready->embeddedOpening)
    object->clips.merge(ready->embeddedOpening->clips);
  return object;
 }
 std::shared_ptr<Composite> object;
 if(readAhead)if(auto ready=readAhead->take(qlie::canonical(path));ready&&ready->composite){
  // Replay the constructor's original allocation order only when the VM loads
  // this container. Speculative branches never consume the scene's PRNG.
  std::vector<uint32_t> ids(ready->identities);
  for(auto&id:ids)id=particleRandomNext(particleState);
  object=std::move(ready->composite);
  object->resolveIdentities(ids);
 }
 if(!object)object=std::make_shared<Composite>(assets.read(path),path,particleState);
 composites.emplace(path,object);
 return object;
}
bool SceneImages::needsWarmFile(const std::string&path,bool upcoming)const{
 if(cache.count(path))return false;
 const auto cached=composites.find(path);
 if(cached==composites.end())return true;
 const auto&object=cached->second;
 return object.use_count()==1&&object->hasVideo&&(upcoming||object->clips.empty());
}
bool SceneImages::warmFile(const std::string&path,bool required,bool prepareVideo){
 if(!readAhead||!needsWarmFile(path,!required))return false;
 if(const auto cached=composites.find(path);cached!=composites.end()){
  if(!prepareVideo)return false;
  // Only immutable encoded data is shared with the worker. The cached
  // composite and any decoder still in use belong to the scene thread.
  auto sources=cached->second->openingVideoSources();
  readAhead->request(qlie::canonical(path),[sources=std::move(sources),nativeVideo=prefetchNativeVideo]{
   WarmAsset ready;
   ready.embeddedOpening=std::make_shared<PreparedEmbeddedOpening>(sources,nativeVideo);
   ready.bytes=ready.embeddedOpening->bytesHeld();
   return ready;
  },required);
  return true;
 }
 readAhead->request(qlie::canonical(path),[&a=assets,path,prepareVideo,nativeVideo=prefetchNativeVideo]{
  WarmAsset ready;auto bytes=a.read(path);
  if(path.size()>2&&path.substr(path.size()-2)==".b"){
   ready.composite=std::make_shared<Composite>(bytes,path,ready.identities,true);
   if(prepareVideo&&ready.composite->hasVideo)ready.composite->prepareOpeningVideo(nativeVideo);
   ready.bytes=ready.composite->bytesHeld();
  }else{ready.bitmap=sceneBitmap(image(bytes));ready.bytes=ready.bitmap->pixels.size();}
  return ready;
 },required);
 return true;
}
std::string SceneImages::BackgroundBlur::key()const{
 return "@background-blur/"+std::to_string(source->version.get())+"/"+name;
}
std::optional<SceneImages::BackgroundBlur> SceneImages::backgroundBlur(const std::string&line)const{
 if(!readAhead||line.rfind("^bg",0)!=0||line.find(",imgfilter:blur")==line.npos)return {};
 auto comma=line.find(',');
 auto layer=line.substr(1,comma-1);
 auto found=layers.find(layer);
 if(found==layers.end())return {};
 const auto&current=found->second;
 if(!current.pixels||current.composite)return {};
 std::string filter;
 // Only precompute filters that operate on the currently loaded bitmap.
 // Commands replacing the image or expanding a template use the normal path.
 for(const auto&field:split(line.substr(comma+1),',')){
  auto colon=field.find(':');
  if(colon==field.npos)return {};
  auto name=field.substr(0,colon);
  if(name=="file"||name=="clear")return {};
  if(name=="imgfilter")filter=field.substr(colon+1);
 }
 if(filter!="blur10"&&filter!="blur20")return {};
 const auto&cached=current.filtered[0];
 if(cached.source==current.pixels&&cached.name==filter)return {};
 return BackgroundBlur{std::move(layer),std::move(filter),current.pixels};
}
void SceneImages::warmBackgroundBlur(const BackgroundBlur&blur,bool required){
 readAhead->request(blur.key(),[source=blur.source,name=blur.name]{
  WarmAsset ready;
  ready.bitmap=sceneBitmap(filterImage(*source,name));
  ready.bytes=ready.bitmap->pixels.size();
  return ready;
 },required);
}
void SceneImages::installBackgroundBlur(const BackgroundBlur&blur){
 if(auto ready=readAhead->takeReady(blur.key()))
  layers.at(blur.layer).filtered[0]={blur.source,std::move(ready->bitmap),blur.name};
}
std::vector<std::string> SceneImages::warmPaths(const std::string&line,CharacterPrefixes* future)const{
 std::vector<std::string> result;auto comma=line.find(',');if(comma==line.npos)return result;
 auto cmd=line.substr(1,comma-1);
 bool chara=cmd.rfind("chara",0)==0,face=cmd=="face",message=cmd=="message";
 if(!chara&&!face&&!message&&cmd.rfind("bg",0)!=0&&cmd.rfind("effect",0)!=0)return result;
 auto layer=layers.find(cmd);
 std::string stem=chara&&layer!=layers.end()?layer->second.prefix:"";
 if(chara&&future){auto saved=future->find(cmd);if(saved!=future->end())stem=saved->second;}
 std::string base;
 // An authored template can supply the window, effect or actor's motion file.
 auto args=split(expand(line.substr(comma+1),chara?"chara":face?"face":message?"message":"image"),',');
 for(auto&field:args){
  if(field=="clear:def"){base.clear();if(chara)stem.clear();}
  auto sep=field.find(':');if(sep==field.npos)continue;
  auto key=field.substr(0,sep),name=field.substr(sep+1);
  if((chara||face)&&key=="base"){base=std::move(name);continue;}
  if(name.empty()||name=="none"||name=="none:none"||name.find('$')!=name.npos)continue;
  bool replacement=key.size()==4&&key.rfind("rep",0)==0;
  if(key!="file"&&!replacement)continue;
  std::string prefix="image/";
  if(!replacement){
   if(chara){prefix="chara/";name=characterImageName(name,stem);}
   else if(face)prefix="face/";else if(message)prefix="avg/window/";
  }
  auto path=prefix+name;
  if(!assets.inPack(path)){if(assets.inPack(path+".png"))path+=".png";else if(assets.inPack(path+".b"))path+=".b";else continue;}
  if(needsWarmFile(path,future!=nullptr))result.push_back(path);
  if(future&&chara&&!replacement){
   const auto portrait=faceResource(mappedFace(path),true);
   if(!portrait.empty()&&!cache.count(portrait)&&!composites.count(portrait))result.push_back(portrait);
  }
 }
 if(!base.empty()&&base!="none"&&base.find('$')==base.npos){
  auto path=(face?"face/":"chara/")+base;
  if(!assets.inPack(path))path+=".b";
  if(assets.inPack(path)&&needsWarmFile(path,future!=nullptr))result.push_back(std::move(path));
 }
 if(chara&&future)future->insert_or_assign(cmd,std::move(stem));
 return result;
}
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
std::string SceneImages::expand(std::string s,const std::string&type)const{
 if(s.find('$')==s.npos)return s;
 auto table=templates.find(type);if(table==templates.end())return s;
 std::vector<std::pair<std::string,std::string>>values(table->second.begin(),table->second.end());
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
 auto at=cache.find(path);if(at==cache.end())at=cache.emplace(path,loadBitmap(path)).first;
 layer.scriptReplacement=at->second;
}
void SceneImages::prepareUiBitmap(const std::string&path){if(!cache.count(path))cache.emplace(path,loadBitmap(path));}
void SceneImages::uiSlot(const std::string&name,unsigned slot,const std::string&path){if(name.rfind("ui_",0)!=0)throw std::runtime_error("UI slot owner");prepareUiBitmap(path);layers.at(name).uiSlots[slot]=cache.at(path);}
void SceneImages::uiSlot(const std::string&name,unsigned slot,const Image&value){if(name.rfind("ui_",0)!=0)throw std::runtime_error("UI slot owner");layers.at(name).uiSlots[slot]=sceneBitmap(value);}
void SceneImages::setUiMotion(const std::string&name,const char*motion){
 if(name.rfind("ui_",0)!=0)throw std::runtime_error("UI motion owner");
 auto&layer=layers.at(name);
 auto&current=layer.params["motion"];
 if(current==motion)return;
 current=motion;layer.milliseconds=0;layer.audioMilliseconds=-1;
}
void SceneImages::removeUiLayer(const std::string&name,bool releaseAsset){
 if(name.rfind("ui_",0)!=0)throw std::runtime_error("UI layer owner");
 auto layer=layers.find(name);
 if(layer==layers.end())return;
 auto path=std::move(layer->second.path);
 layers.erase(layer);
 if(!releaseAsset)return;
 if(auto bitmap=cache.find(path);bitmap!=cache.end()&&bitmap->second.use_count()==1)cache.erase(bitmap);
 if(auto composite=composites.find(path);composite!=composites.end()&&composite->second.use_count()==1){
  if(readAhead&&composite->second->hasVideo)readAhead->discard(qlie::canonical(path));
  composite->second->closeVideoClips(readAhead);
  composites.erase(composite);
 }
}
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
void SceneImages::clearComposites(){
 if(readAhead)for(auto&entry:composites)if(entry.second->hasVideo){
  readAhead->discard(qlie::canonical(entry.first));
  entry.second->closeVideoClips(readAhead);
 }
 composites.clear();
}
void SceneImages::clear(){
 const auto start=qlie::diagnostics?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
 const auto layerCount=layers.size(),bitmaps=cache.size(),animations=composites.size();
 layers.clear();
 cache.clear();
 clearComposites();
 if constexpr (qlie::diagnostics){
  const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(ms>=1000.0/60)
   QLIE_LOGF("LOAD scene_clear_ms=%.3f layers=%zu bitmaps=%zu animations=%zu\n",ms,layerCount,bitmaps,animations);
 }
 }
void SceneImages::seekUiLayer(const std::string&name,double milliseconds){
 if(name.rfind("ui_",0)!=0)throw std::runtime_error("UI animation owner");
 auto&layer=layers.at(name);
 layer.milliseconds=milliseconds;layer.audioMilliseconds=milliseconds;
}
void SceneImages::resetUiAnimations(){
 for(auto&entry:layers){entry.second.milliseconds=0;entry.second.audioMilliseconds=-1;}
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
  else {QLIE_LOG_LINE(<<"PENDING-IMAGE-TWEEN mode="<<m->second<<"\n");mode=0;}
 }
 float power=layer.params.count("movepow")?numeric(layer.params.at("movepow")):100;
 auto add=[&](const std::string&key,float fallback){float from=value(before,key,fallback);auto found=layer.params.find(key);float to=found==layer.params.end()?fallback:numeric(found->second);
  if(key=="x"||key=="y"||key=="z"){auto axial=layer.params.find(key=="x"?"ax":key=="y"?"ay":"az");if(axial!=layer.params.end())to+=numeric(axial->second);}
  if(duration>0&&from!=to)layer.tweens[key]={from,to,power,0,duration,mode};else {layer.tweens.erase(key);if(key=="x"||key=="y"||key=="z")layer.tweens[key]={to,to,power,0,0,mode};}
 };
 if(position){add("x",640);add("y",360);add("z",0);add("scalex",100);add("scaley",100);add("scalez",100);add("rotx",0);add("roty",0);add("rotz",0);add("pri",0);}
 if(color){add("alpha",255);add("addcolor",0);add("mulcolor",0xffffff);}
}
void SceneImages::update(double ms){for(auto&entry:layers){auto&layer=entry.second;
 double speed=layer.params.count("speed")?numeric(layer.params.at("speed")):100;
 // The portrait exits at 3750 ms in the card's motion. Match its exit to
 // the 5000 ms script hold without changing this layer's authored speed.
 if(layer.path==eyecatchPath)speed*=0.75;
 layer.milliseconds+=ms*std::max(0.,speed)/100;
 for(auto&t:layer.tweens)t.second.elapsed=std::min(t.second.duration,t.second.elapsed+ms);
 if(layer.params.count("@hiding")&&layer.params.at("@hiding")=="true"&&layer.composite){auto*motion=findMotion(layer.composite->definition,"*Disable");if(motion){auto samples=sampleMotion(*motion,layer.milliseconds);if(std::none_of(samples.begin(),samples.end(),[](auto&sample){return sample.kind!=0;})){layer.params["show"]="false";layer.params.erase("@hiding");}}}
 }
 // Keep loaded animation data available, but inactive composites no longer
 // need an open video decoder or its decoded frame.
 const auto start=qlie::diagnostics?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
 size_t closed=0;
 for(const auto&entry:composites)if(entry.second.use_count()==1)closed+=entry.second->closeVideoClips(readAhead);
 if(qlie::diagnostics&&closed){
  const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(ms>=1000.0/60)QLIE_LOGF("LOAD video_cleanup_ms=%.3f clips=%zu\n",ms,closed);
 }
}
bool SceneImages::command(const std::string&line){auto p=line.find(44);
 auto cmd=line.substr(1,p-1);
 bool chara=cmd.rfind("chara",0)==0,face=cmd=="face",message=cmd=="message",ui=cmd.rfind("ui_",0)==0;
 if(!ui&&!chara&&!face&&!message&&cmd.rfind("bg",0)!=0&&cmd!="effect"&&cmd!="env")return false;
 auto&l=layers[cmd];
 const std::map<std::string,std::string> faceDefaults{{"mode","auto"},{"disp","once"},{"show","false"},{"x","0"},{"y","0"},{"scalex","100"},{"scaley","100"},{"alpha","255"},{"pri","0"},{"align","left:top"},{"@face-active","false"}};
 // Imo_Image_DefaultParam is merged by clear:def, including animation state.
 static const std::map<std::string,std::string> imageDefaults{
  {"show","false"},{"x","0"},{"y","0"},{"z","0"},{"ax","0"},{"ay","0"},{"az","0"},
  {"scalex","100"},{"scaley","100"},{"scalez","100"},{"rotx","0"},{"roty","0"},{"rotz","0"},
  {"pri","0"},{"align","left:top"},{"motion","0"},{"time","0"},{"move","linear"},{"movepow","100"},
  {"file","none"},{"texfilter","pass"},{"alpha","$FF"},{"addcolor","$000000"},{"mulcolor","$FFFFFF"},
  {"speed","100"},{"imgfilter","none"},{"vol","100"},{"voltype","se"},{"dchannel","0"}};
 // Imo_Message_DefaultParam used by the scripted clear:def command.
 static const std::map<std::string,std::string> messageDefaults{
  {"mode","cls"},{"show","false"},{"x","0"},{"y","0"},{"z","0"},
  {"scalex","100"},{"scaley","100"},{"alpha","255"},{"pri","1100"},
  {"align","left:top"},{"motion","0"},{"file","none"},
  {"textarea","800:600"},{"fontsize","24:24"},{"fontcolor","$FFFFFF"},{"fontno","0"}};
 // Imo_Chara_DefaultParam uses a visible, bottom-centred empty layer.
 static const auto charaDefaults=[] {
  auto defaults=imageDefaults;
  defaults["show"]="true";
  defaults["align"]="center:bottom";
  defaults["base"]="none";
  defaults["id"]="none";
  defaults.erase("voltype");
  return defaults;
 }();
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
 if(key=="clear"&&v=="def"&&!face&&!ui){
  l=Layer{};
  l.params=message?messageDefaults:chara?charaDefaults:imageDefaults;
  file=baseChanged=position=color=messageShow=false;
  std::fill(std::begin(replacementChanged),std::end(replacementChanged),false);continue;
 }
 if(face&&key=="clear"&&v=="def"){l=Layer{};l.params=faceDefaults;continue;}
 if(key=="imgfilter"&&(!l.params.count(key)||l.params.at(key)!=v))l.filtered={};
 if(key=="clear"){l.filtered={};l.params.erase("@portrait");l.params.erase("@replacement");l.scriptReplacement.reset();l.path.clear();
 l.pixels.reset();
 l.composite.reset();
 l.params["show"]="false";
 continue;
 }l.params[key]=v;
 if(message&&key=="show")messageShow=true;
 if(key=="file"){l.filtered={};file=true;if(face)l.params["@face-active"]="true";}
 if((chara||face)&&key=="base")baseChanged=true;
 if(key=="x"||key=="y"||key=="z"||key=="ax"||key=="ay"||key=="az"||key=="scalex"||key=="scaley"||key=="scalez"||key=="rotx"||key=="roty"||key=="rotz"||key=="pri")position=true;
 if(key=="alpha"||key=="mulcolor"||key=="addcolor")color=true;
 if(key=="vol"&&std::stof(v)==0)l.audioStopPending=true;
 if(key=="motion"){l.milliseconds=0;l.audioMilliseconds=-1;}
 if(key!="rep0"&&key!="rep1"&&key!="rep2"&&key!="rep3"&&key!="textarea"&&key!="fontsize"&&key!="fontcolor"&&key!="fontno"&&key!="imgfilter"&&key!="trans"&&key!="id"&&key!="mode"&&key!="disp"&&key!="base"&&key!="time"&&key!="speed"&&key!="move"&&key!="movepow"&&key!="ax"&&key!="ay"&&key!="az"&&key!="z"&&key!="scalez"&&key!="rotx"&&key!="roty"&&key!="vol"&&key!="voltype"&&key!="dchannel"&&key!="motion"&&key!="file"&&key!="x"&&key!="y"&&key!="scalex"&&key!="scaley"&&key!="alpha"&&key!="show"&&key!="pri"&&key!="align"&&key!="texfilter"&&key!="rotz"&&key!="mulcolor"&&key!="addcolor")QLIE_LOG_LINE(<<"PENDING-IMAGE-PROPERTY "<<cmd<<" "<<key<<"="<<v<<"\n");
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
  l.composite=sceneComposite(path);l.path=path;l.milliseconds=0;l.audioMilliseconds=-1;return true;
 };
 if(baseChanged&&!file){if(!loadBase()){l.composite.reset();auto portrait=l.params.find("@portrait");l.path=portrait==l.params.end()?"":portrait->second;}return true;}
 if(file){std::string f=l.params["file"];
 if(f=="none"||f=="none:none"){l.params.erase("@portrait");l.params.erase("@replacement");l.scriptReplacement.reset();l.path.clear();
 l.pixels.reset();
 l.composite.reset();
 return true;
 }if(chara)f=characterImageName(f,l.prefix);
 std::string path=(ui?"":chara?"chara/":face?"face/":message?"avg/window/":"image/")+f;
 if(!assets.exists(path)){if(assets.exists(path+".png"))path+=".png";
 else if(assets.exists(path+".b"))path+=".b";
 else throw std::runtime_error("Missing image "+path);
 }l.path=path;
 if(chara||face)l.params["@portrait"]=path;
 if(path.size()>2&&path.substr(path.size()-2)==".b"){l.pixels.reset();
 l.composite=sceneComposite(path);
 l.milliseconds=0;
 l.audioMilliseconds=-1;
 return true;
 }auto it=cache.find(path);
 const bool cacheMiss=it==cache.end();
 if(cacheMiss)it=cache.emplace(path,loadBitmap(path)).first;
 l.pixels=it->second;
 if(loadBase())return true;
 l.composite.reset();
 l.milliseconds=0;
 l.audioMilliseconds=-1;
 if(qlie::diagnostics&&cacheMiss){
  std::ostringstream line;
  line<<"IMAGE loaded "<<path<<' '<<l.pixels->width<<'x'<<l.pixels->height<<'\n';
  const auto message=line.str();
  std::fwrite(message.data(),1,message.size(),stderr);
 }
 }return true;
 }
void SceneImages::beginPage(){faceWarning=false;auto i=layers.find("face");if(i!=layers.end())i->second.params["@face-active"]="false";}
std::string SceneImages::mappedFace(std::string file)const{
 if(file.rfind("chara/",0)==0)file.erase(0,6);
 for(auto&entry:faceRepFile){size_t pos=0;while((pos=file.find(entry.first,pos))!=file.npos){file.replace(pos,entry.first.size(),entry.second);pos+=entry.second.size();}}
 return file;
}
std::string SceneImages::faceResource(const std::string&name,bool packedOnly)const{
 for(auto ext:{"",".png",".b",".argb"}){
  auto path="face/"+name+ext;
  if(packedOnly?assets.inPack(path):assets.exists(path))return path;
 }
 return {};
}
std::optional<std::string> SceneImages::selectFace(const std::string&speaker,const std::string&voice,bool deferCold){
 auto face=layers.find("face");if(face==layers.end())return {};auto&params=face->second.params;
 if(params.at("mode")!="auto"||params.at("show")!="true"||params.at("@face-active")=="true")return {};
 faceWarning=true;std::string id=faceType?voice:speaker;
 if(id.rfind("【",0)==0){id.erase(0,std::string("【").size());auto end=id.find("】");if(end!=id.npos)id.resize(end);}
 if(id.rfind("％",0)==0)id.erase(0,std::string("％").size());
 for(auto&entry:faceRepId)if(id.find(entry.first)!=id.npos){id=entry.second;break;}
 auto suffix=id.find("＠");if(suffix!=id.npos)id.resize(suffix);if(id.empty())return {};
 std::string file;
 for(auto&entry:layers)if(entry.first.rfind("chara",0)==0&&entry.second.params.count("id")&&entry.second.params.at("id")==id&&entry.second.params.count("@portrait")){file=entry.second.params.at("@portrait");break;}
 if(file.empty())for(auto&entry:layers)if(entry.first.rfind("chara",0)==0&&entry.second.params.count("@portrait")&&entry.second.params.at("@portrait").find(id)!=id.npos){file=entry.second.params.at("@portrait");break;}
 if(file.empty())return {};
 file=mappedFace(std::move(file));
 auto resolved=faceResource(file,false);
 if(resolved.empty()&&!faceDefaultFile.empty()){auto fallback=faceDefaultFile;size_t pos=0;while((pos=fallback.find("[id]"))!=fallback.npos){fallback.replace(pos,4,id);pos+=id.size();}resolved=faceResource(fallback,false);}
 if(resolved.empty()){QLIE_LOG_LINE(<<"PENDING-FACE unresolved candidate="<<file<<"\n");return {};}
 if(deferCold&&readAhead&&!cache.count(resolved)&&!composites.count(resolved)&&!readAhead->ready(qlie::canonical(resolved)))return resolved;
 command("^face,file:"+resolved.substr(5));
 return {};
}
std::pair<unsigned,unsigned> SceneImages::dimensions(const std::string&name)const{const auto&layer=layers.at(name);if(layer.composite)return {unsigned(layer.composite->definition.bounds[0]),unsigned(layer.composite->definition.bounds[1])};if(layer.pixels)return {layer.pixels->width,layer.pixels->height};throw std::runtime_error("Image dimensions unavailable: "+name);}
void SceneImages::draw(qlie::Bytes&out,bool liveVideo,const Affine2D*view)const{render(out,0,nullptr,999,liveVideo,view);}
void SceneImages::drawThumbnail(qlie::Bytes&out,float maxPriority)const{render(out,4,nullptr,maxPriority);}
void SceneImages::drawScene(qlie::Bytes&out,bool liveVideo)const{render(out,1,nullptr,999,liveVideo);}
void SceneImages::drawFaces(qlie::Bytes&out)const{render(out,2);}
void SceneImages::drawMessages(qlie::Bytes&out)const{render(out,3);}
MessageStyle SceneImages::messageStyle()const{MessageStyle style;auto it=layers.find("message");if(it==layers.end())return style;auto&params=it->second.params;
 auto pair=[&](const std::string&key,unsigned&a,unsigned&b){auto found=params.find(key);if(found==params.end())return;auto values=split(found->second,':');a=unsigned(numeric(values[0]));b=values.size()>1?unsigned(numeric(values[1])):a;};
 pair("textarea",style.width,style.height);pair("fontsize",style.fontWidth,style.fontHeight);auto color=params.find("fontcolor");if(color!=params.end())style.color=uint32_t(numeric(color->second));return style;
}
bool SceneImages::messageVisible()const{auto it=layers.find("message");return it!=layers.end()&&it->second.params.at("show")=="true";}
void SceneImages::setMessageImage(Image image,bool show){setMessageImage(sceneBitmap(std::move(image)),show);}
void SceneImages::setMessageImage(std::shared_ptr<const Image>image,bool show){
 if(!layers.count("message"))command("^message,$window01");
 auto&layer=layers.at("message");
 layer.pixels=std::move(image);
 layer.filtered[1]={};
 if(show&&(layer.params["show"]!="true"||layer.params.count("@hiding"))){if(layer.composite&&findMotion(layer.composite->definition,"*Enable")){layer.params["motion"]="*Enable";layer.milliseconds=0;layer.audioMilliseconds=-1;}layer.params["show"]="true";layer.params.erase("@hiding");}
}

void SceneImages::draw(RenderTarget&target,bool liveVideo,const Affine2D*view)const{qlie::Bytes unused;render(unused,0,&target,999,liveVideo,view);}
void SceneImages::drawScene(RenderTarget&target,bool liveVideo)const{qlie::Bytes unused;render(unused,1,&target,999,liveVideo);}
void SceneImages::drawFaces(RenderTarget&target)const{qlie::Bytes unused;render(unused,2,&target);}
void SceneImages::drawMessages(RenderTarget&target)const{qlie::Bytes unused;render(unused,3,&target);}
RenderQueue SceneImages::captureQueue(bool shaderMasks,bool nativeVideo)const{
 RenderQueue queue(shaderMasks,nativeVideo);
 enqueue(queue,1,999,true);
 return queue;
}
void SceneImages::render(qlie::Bytes&out,unsigned pass,RenderTarget*target,float maxPriority,bool liveVideo,const Affine2D*view)const{
 if(!target&&out.size()!=1280*720*4)throw std::runtime_error("Scene canvas size");
 RenderQueue queue(target&&target->supportsMasks(),target&&target->supportsVideoSurfaces(),view);
 enqueue(queue,pass,maxPriority,liveVideo);
 if(target)queue.draw(*target,1280,720);else queue.draw(out,1280,720);
}
void SceneImages::enqueue(RenderQueue&queue,unsigned pass,float maxPriority,bool liveVideo)const{
 std::vector<const Layer*>sorted;
 for(auto&x:layers){auto&layer=x.second;bool face=x.first=="face",message=x.first=="message";
 if((pass==1&&(face||message))||(pass==2&&!face)||(pass==3&&!message)||(pass==4&&value(layer,"pri",0)>maxPriority))continue;
 if(face&&layer.params.at("disp")!="keep"&&layer.params.at("@face-active")!="true"){
 if(layer.params.at("show")=="true"&&layer.params.at("mode")=="auto"&&!faceWarning){QLIE_LOG_LINE(<<"PENDING-FACE automatic portrait selection\n");faceWarning=true;}continue;
 }
 auto base=layer.params.find("base");
 if(base!=layer.params.end()&&!base->second.empty()&&base->second!="none"&&!layer.params.count("@portrait"))continue;
 if((layer.pixels||layer.composite)&&layer.params.at("show")!="false")sorted.push_back(&layer);
 }
 auto get=[](const Layer*l,const std::string&k,float def){return value(*l,k,def);};
 std::stable_sort(sorted.begin(),sorted.end(),[&](auto*a,auto*b){return get(a,"pri",0)<get(b,"pri",0);});
 for(auto*l:sorted){
 auto beforeImages=queue.imageCount();
 auto align=l->params.at("align");
 float ax=align.rfind("center",0)==0?0.5f:align.rfind("right",0)==0?1.f:0.f,ay=align.find(":bottom")!=align.npos?1.f:align.find(":center")!=align.npos?0.5f:0.f;
 unsigned w=l->composite?unsigned(l->composite->definition.bounds[0]):l->pixels->width,h=l->composite?unsigned(l->composite->definition.bounds[1]):l->pixels->height;
 auto placement=imagePlacement(get(l,"x",640),get(l,"y",360),get(l,"scalex",100)/100,get(l,"scaley",100)/100,get(l,"rotz",0),ax,ay,w,h);
 AnimationSample transform;transform.x=get(l,"x",640);transform.y=get(l,"y",360);transform.z=get(l,"z",0);transform.scaleX=get(l,"scalex",100);transform.scaleY=get(l,"scaley",100);transform.scaleZ=get(l,"scalez",100);transform.rotationX=get(l,"rotx",0);transform.rotationY=get(l,"roty",0);transform.rotationZ=get(l,"rotz",0);transform.anchorX=ax*100;transform.anchorY=ay*100;
 bool spatial=transform.z!=0||transform.rotationX!=0||transform.rotationY!=0||transform.scaleZ!=100;
 Matrix3D matrix;if(spatial)matrix=placement3D(transform,w,h);
 std::map<unsigned,const Image*> uiSlots;for(auto&slot:l->uiSlots)uiSlots[slot.first]=slot.second.get();if(l->scriptReplacement)uiSlots[0]=l->scriptReplacement.get();
 if(l->path==eyecatchPath&&l->composite&&!uiSlots.count(0))
  l->composite->loadEyecatchFallback();
 std::string filter=l->params.count("imgfilter")?l->params.at("imgfilter"):"none";
 auto filtered=[&](unsigned slot,const std::shared_ptr<const Image>&source)->const Image*{
  auto&cached=l->filtered[slot];if(filter.empty()||filter=="none"){cached={};return source.get();}
  if(cached.source!=source||cached.name!=filter){auto result=sceneBitmap(filterImage(*source,filter));cached.source=source;cached.name=filter;cached.result=std::move(result);}
  return cached.result.get();
 };
 const Image*plain=l->pixels.get();
 if(l->composite){if(filter.empty()||filter=="none"){for(auto&cached:l->filtered)cached={};}else for(unsigned i=0;i<4;i++){
  std::shared_ptr<const Image> source=i<l->composite->members.size()?l->composite->members[i].image:nullptr;
  if(i==0&&l->params.count("@portrait"))source=l->pixels;
  if(i==1&&layers.count("message")&&l==&layers.at("message"))source=l->pixels;
  auto slot=l->uiSlots.find(i);if(slot!=l->uiSlots.end())source=slot->second;
  if(i==0&&l->scriptReplacement)source=l->scriptReplacement;
  if(source)uiSlots[i]=filtered(i,source);
  else if(i<l->composite->members.size()){auto&e=l->composite->members[i];if(e.nested||e.mesh||e.video)l->composite->warn("image-filter-nonbitmap-slot="+std::to_string(i));}
 }}else if(l->pixels)plain=filtered(0,l->pixels);
 AnimationContext external;external.randomCursor=&waveformCursor;for(unsigned i=0;i<16;i++){auto v=l->params.find("@exv"+std::to_string(i));if(v!=l->params.end()){external.external[i]=numeric(v->second);external.externalMask|=uint16_t(1u<<i);}}
 if(l->composite){auto m=l->params.find("motion");l->composite->enqueue(queue,l->milliseconds,m==l->params.end()?"":m->second,placement,get(l,"pri",0),get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)),0,l->params.count("@portrait")?l->pixels.get():nullptr,layers.count("message")&&l==(&layers.at("message"))?l->pixels.get():nullptr,windowAlpha,uiSlots.empty()?nullptr:&uiSlots,&external,spatial?&matrix:nullptr,unsigned(get(l,"texfilter",0)),liveVideo);}
 else if(spatial){transform.alpha=get(l,"alpha",255);transform.addColor=uint32_t(get(l,"addcolor",0));transform.mulColor=uint32_t(get(l,"mulcolor",0xffffff));transform.textureFilter=unsigned(get(l,"texfilter",0));quad(queue,*plain,matrix,transform,get(l,"pri",0));}
 else queue.image(*plain,placement,get(l,"pri",0),get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)),0,unsigned(get(l,"texfilter",0)));
 if(queue.imageCount()>beforeImages){if(qlie::canonical(l->path).rfind("image/ev/",0)==0)shownFiles.insert(qlie::canonical(l->path));auto replacement=l->params.find("@replacement");if(replacement!=l->params.end()&&qlie::canonical(replacement->second).rfind("image/ev/",0)==0)shownFiles.insert(qlie::canonical(replacement->second));}
 }
}

qlie::Bytes SceneImages::save()const{StateWriter w;
 w.str("WHJ2-images-7");
 w.n(particleState);
 w.n(waveformCursor);
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
SceneImages::SceneImages(const SceneImages&live,RestoreTarget)
 :windowAlpha(live.windowAlpha),particleState(live.particleState),
  faceRepId(live.faceRepId),faceRepFile(live.faceRepFile),faceType(live.faceType),faceDefaultFile(live.faceDefaultFile),
  assets(live.assets),templates(live.templates),cache(live.cache),prefetchNativeVideo(live.prefetchNativeVideo){}
std::unique_ptr<SceneImages> SceneImages::makeRestoreTarget()const{
 // Bitmap pixels are immutable. Live layers, decoders and speculative
 // requests never become part of the worker's restore target.
 return std::unique_ptr<SceneImages>(new SceneImages(*this,RestoreTarget{}));
}
void SceneImages::restore(const qlie::Bytes&b){StateReader r{b};
 auto version=r.str();bool v7=version=="WHJ2-images-7",v6=v7||version=="WHJ2-images-6",v5=v6||version=="WHJ2-images-5",v4=v5||version=="WHJ2-images-4",v3=v4||version=="WHJ2-images-3";
 if(!v3&&version!="WHJ2-images-2")throw std::runtime_error("Image state version");
 auto savedParticleState=v3?uint32_t(r.n()):particleState;
 const auto savedWaveformCursor=v7?r.n():0;
 if(savedWaveformCursor>1023)throw std::runtime_error("Image waveform cursor");
 std::map<std::string,Layer>next;
 auto count=r.n();
 if(count>256)throw std::runtime_error("Image layer count");
 // Cached future assets must not reuse identities from the discarded timeline.
 clearComposites();
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
 }if(!l.path.empty()&&l.path.substr(l.path.size()-2)==".b"){
 l.composite=sceneComposite(l.path);
 }else if(!l.path.empty()){auto it=cache.find(l.path);
 if(it==cache.end())it=cache.emplace(l.path,loadBitmap(l.path)).first;
 l.pixels=it->second;
 }if(l.composite&&l.params.count("@portrait")&&l.path!=l.params.at("@portrait")){auto portrait=l.params.at("@portrait");auto it=cache.find(portrait);if(it==cache.end())it=cache.emplace(portrait,loadBitmap(portrait)).first;l.pixels=it->second;}
 if(v3&&l.composite)l.composite->restoreIdentity(r);
 if(l.params.count("@replacement"))loadReplacement(l,l.params.at("@replacement"));
 next[name]=std::move(l);
 }r.end();
 layers=std::move(next);
 particleState=savedParticleState;
 waveformCursor=uint32_t(savedWaveformCursor);
 }

}
