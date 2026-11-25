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
#include <charconv>
#include <cstdio>
#include <iostream>
#include <cmath>
#include <cstring>
#include <mutex>
#include <optional>
namespace whj2 {
namespace {
float numeric(const std::string&s){return !s.empty()&&s[0]=='$'?float(std::stoul(s.substr(1),nullptr,16)):std::stof(s);
 }
std::string characterImageName(const std::string&name,std::string&prefix){
 auto parts=split(name,':');
 if(parts.size()>1)prefix=parts[1];
 return parts[0].find('/')==std::string::npos?prefix+parts[0]:parts[0];
}
bool characterPart(const std::string&key){
 return key.size()==5&&key.compare(0,4,"file")==0&&key.back()>='0'&&key.back()<='7';
}
bool collectCharacterParts(std::vector<std::string>&args,std::map<std::string,std::string>&parts){
 bool changed=false;
 auto end=std::remove_if(args.begin(),args.end(),[&](const std::string&field){
  if(field=="clear:def"){parts.clear();changed=false;return false;}
  const auto sep=field.find(':');
  if(sep==field.npos||!characterPart(field.substr(0,sep)))return false;
  parts[field.substr(0,sep)]=field.substr(sep+1);
  changed=true;
  return true;
 });
 args.erase(end,args.end());
 return changed;
}
std::string characterPartsName(std::map<std::string,std::string>&params){
 std::string name;
 bool ended=false;
 for(unsigned i=0;i<8;++i){
  auto part=params.find("file"+std::to_string(i));
  if(part==params.end()){ended=true;continue;}
  ended=ended||part->second=="none";
  if(ended)params.erase(part);
  else name+=part->second;
 }
 return name.empty()?"none":name;
}

// Scene asset and UI-slot owners replace the entire image on change.
std::shared_ptr<const Image> sceneBitmap(Image pixels){
 pixels.version.publish();
 return std::make_shared<const Image>(std::move(pixels));
}
bool sceneCompositePath(const std::string&path){
 const auto key=qlie::canonical(path);
 const auto dot=key.find_last_of('.');
 return dot!=key.npos&&(key.compare(dot,key.npos,".b")==0||key.compare(dot,key.npos,".ogv")==0);
}
std::string sceneResource(qlie::Assets&assets,const std::string&path,bool packedOnly){
 // Imo_Image_File calls Imo_FileSerch with these extensions in order.
 for(const auto extension:{"",".png",".bmp",".jpg",".b",".ogv"}){
  auto candidate=path+extension;
  if(packedOnly?assets.inPack(candidate):assets.exists(candidate))return candidate;
 }
 return {};
}
unsigned imageBlend(const std::string&mode){
 constexpr std::array<const char*,9> names{{"pass","normal","screen","multiple","difference","lighten","darken","add","sub"}};
 if(mode=="lineardodge")return 7;
 if(mode=="hardlight")return 13;
 if(mode=="linearlight")return 15;
 if(mode=="pinlight")return 16;
 auto found=std::find(names.begin(),names.end(),mode);
 if(found==names.end()){QLIE_LOG_LINE(<<"PENDING-IMAGE-BLEND "<<mode<<'\n');return 0;}
 return unsigned(found-names.begin());
}
struct JpegAnimation {
 qlie::Bytes data;
 std::vector<std::pair<size_t,size_t>> frames;
 unsigned duration=0;
 size_t frameIndex=0;
 std::shared_ptr<const Image> decoded;
 explicit JpegAnimation(qlie::Bytes bytes):data(std::move(bytes)){
  auto number=[&](size_t p){return uint32_t(data[p])|(uint32_t(data[p+1])<<8)|(uint32_t(data[p+2])<<16)|(uint32_t(data[p+3])<<24);};
  if(data.size()<64||std::memcmp(data.data()+12,"SOUND",5)||std::memcmp(data.data()+32,"MOVIE",5)||number(24)||number(28))
   throw std::runtime_error("Unsupported IMOAVI header");
  duration=number(44);
  const auto count=number(48);
  if(!duration||!count||count>(data.size()-64)/32)throw std::runtime_error("Invalid IMOAVI frame count or duration");
  frames.reserve(count);
  size_t offset=64;
  for(unsigned i=0;i<count;++i){
   constexpr size_t header=28;
   if(data.size()-offset<header)throw std::runtime_error("Truncated IMOAVI frame header");
   const size_t jpeg=number(offset+20),step=number(offset);
   if(jpeg<4||jpeg>data.size()-offset-header||(step!=header+jpeg&&!(step==0&&i+1==count)))
    throw std::runtime_error("Invalid IMOAVI frame length");
   offset+=header;
   if(data[offset]!=0xff||data[offset+1]!=0xd8||data[offset+jpeg-2]!=0xff||data[offset+jpeg-1]!=0xd9)
    throw std::runtime_error("Invalid IMOAVI JPEG markers");
   frames.emplace_back(offset,jpeg);
   offset+=jpeg;
  }
 }
 const std::shared_ptr<const Image>&at(double ms){
  const auto index=std::min(frames.size()-1,size_t(std::fmod(std::max(0.,ms),duration)*frames.size()/duration));
  if(!decoded||index!=frameIndex){
   const auto [start,length]=frames[index];
   decoded=sceneBitmap(image(qlie::Bytes(data.begin()+start,data.begin()+start+length)));
   frameIndex=index;
  }
  return decoded;
 }
};
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
struct EmbeddedDecoder {
 std::shared_ptr<const qlie::Bytes> bytes;
 std::string source;
 KVideo*video=nullptr;
 std::shared_ptr<Image> frame,spare;
 std::shared_ptr<const Image> heldFrame;
 uint64_t heldTick=0;
 uint64_t ticks=0;
 bool ended=false,audible=false,softwareOnly=false,settled=false;
 explicit EmbeddedDecoder(std::shared_ptr<const qlie::Bytes> b,std::string name={})
  :bytes(std::move(b)),source(std::move(name)){}
 ~EmbeddedDecoder(){if(video)kvideo_close(video);}
 void reset(){
  if(video)kvideo_close(video);
  video=softwareOnly?kvideo_open_mode(bytes->data(),bytes->size(),KVIDEO_SOFTWARE):kvideo_open(bytes->data(),bytes->size());
  if(!video)throw std::runtime_error("Embedded video open failed: "+source);
  const unsigned width=kvideo_source_width(video),height=kvideo_source_height(video);
  frame=std::make_shared<Image>(Image{width,height,qlie::Bytes(size_t(width)*height*4)});
  frame->version.publish();
  spare.reset();
  ticks=0;ended=false;audible=false;
 }
 std::shared_ptr<const Image> at(double ms,bool live,bool nativeVideo,bool loop){
  if(!video)reset();
  const double duration=loop?kvideo_duration_ms(video):0;
  const double time=std::max(0.,ms);
  const uint64_t target=uint64_t((duration>0?std::fmod(time,duration):time)*60/1000);
  if(heldFrame&&target<heldTick)heldFrame.reset();
  bool rewound=false;
  if(target+1<ticks){
   if(kvideo_rewind(video)<0)reset();
   else{ticks=0;ended=false;audible=false;rewound=true;}
  }
  settled=ended||ticks>target;
  if(settled)return heldFrame?heldFrame:frame;
  if(nativeVideo&&!softwareOnly)kvideo_enable_surface(video);
  // A long stall can move the script clock ahead by seconds. Work through
  // that backlog in small batches on the interactive presentation path.
  const uint64_t due=live?std::min(target,ticks+5):target;
  const bool borrowed=frame.use_count()>1;
  auto output=frame;
  if(borrowed){
   output=spare&&spare.use_count()==1?std::move(spare):
    std::make_shared<Image>(Image{frame->width,frame->height,qlie::Bytes(frame->pixels.size())});
  }
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
    return at(ms,live,nativeVideo,loop);
   }
   if(rewound){reset();return at(ms,live,nativeVideo,loop);}
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
   if(borrowed)spare=std::move(frame);
   frame=std::move(output);
  }else if(rewound){
   std::fill(output->pixels.begin(),output->pixels.end(),0);
   output->videoSurface.reset();
   output->version.publish();
   if(borrowed)spare=std::move(frame);
   frame=std::move(output);
  }else if(borrowed)spare=std::move(output);
  // A reusable CPU buffer must not keep an extra NVDEC surface pinned.
  if(spare&&spare.use_count()==1)spare->videoSurface.reset();
  ticks=due+1;ended=code==1;settled=ended||due==target;
  if(heldFrame&&(due>=heldTick||ended))heldFrame.reset();
  return heldFrame?heldFrame:frame;
 }
};
struct EmbeddedVideo {
 std::shared_ptr<EmbeddedDecoder> decoder;
 std::shared_ptr<const Image> shown;
 std::optional<uint64_t> shownTick;
 std::optional<uint64_t> pendingTick;
 std::string key;
 bool audible=false,settled=false;
 explicit EmbeddedVideo(std::shared_ptr<const qlie::Bytes> bytes,std::string source)
  :decoder(std::make_shared<EmbeddedDecoder>(std::move(bytes),std::move(source))){}
 void install(std::shared_ptr<const Image> frame,uint64_t tick){
  shown=std::move(frame);shownTick=tick;audible=decoder->audible;settled=decoder->settled;
 }
 void cancel(AssetReadAhead*worker){
  if(pendingTick&&worker)worker->discard(key);
  pendingTick.reset();
 }
 std::shared_ptr<const Image> at(double ms,bool live,bool nativeVideo,bool loop,AssetReadAhead*worker=nullptr,bool async=true){
  const auto tick=uint64_t(std::max(0.,ms)*60/1000);
  if(pendingTick){
   auto ready=live&&async?worker->takeReady(key):worker->take(key);
   if(!ready)return shown;
   install(std::move(ready->bitmap),*pendingTick);
   pendingTick.reset();
  }
  if(settled&&shownTick==tick)return shown;
  if(!live||!worker||!async){
   install(decoder->at(ms,live,nativeVideo,loop),tick);
   return shown;
  }
  if(key.empty())key="@embedded-frame/"+std::to_string(reinterpret_cast<uintptr_t>(this));
  worker->request(key,[decoder=decoder,ms,nativeVideo,loop]{
   WarmAsset ready;
   ready.bitmap=decoder->at(ms,true,nativeVideo,loop);
   return ready;
  },true);
  pendingTick=tick;
  return shown;
 }
};
struct PreparedEmbeddedOpening {
 struct Source {size_t layer;std::shared_ptr<const qlie::Bytes> bytes;std::string name;};
 std::map<size_t,std::unique_ptr<EmbeddedVideo>> clips;
 PreparedEmbeddedOpening(const std::vector<Source>&sources,bool nativeVideo){
  for(const auto&source:sources){
   auto clip=std::make_unique<EmbeddedVideo>(source.bytes,source.name);
   clip->at(0,false,nativeVideo,false);
   clips.emplace(source.layer,std::move(clip));
  }
 }
 size_t bytesHeld()const{
  size_t size=0;
  for(const auto&clip:clips)size+=clip.second->shown->pixels.size();
  return size;
 }
};
namespace {
constexpr auto eyecatchPath="image/cut/eyecatch01.b";
constexpr auto eyecatchFilmPath="image/cut/eyecatch01.b#film#film";
}
struct EyecatchFilm {
 std::mutex mutex;
 qlie::Bytes source;
 std::shared_ptr<const Image> bitmap;

 std::shared_ptr<const Image> decode(qlie::Bytes data){
  std::lock_guard<std::mutex> lock(mutex);
  if(!bitmap||source!=data){
   bitmap=sceneBitmap(image(data));
   source=std::move(data);
  }
  return bitmap;
 }
};
struct Composite {
 struct MaterialDraw {size_t slot;unsigned alpha;uint32_t tint;};
 struct Element {std::shared_ptr<const Image>image;std::shared_ptr<Model>mesh;std::string name;
 std::shared_ptr<Composite>nested;
 std::shared_ptr<const qlie::Bytes> video;
 std::shared_ptr<JpegAnimation> jpegAnimation;
 std::vector<MaterialDraw> materialDraw;
 };
 Animation definition;
 std::array<float,16> externalDefaults{};
 uint16_t externalDefaultMask=0;
 bool hasVideo=false,standaloneVideo=false;
 uint32_t identity=0;std::vector<uint32_t>motionIdentities;
 std::vector<Element>members;
 std::vector<std::pair<unsigned,unsigned>> linkedSlots;
 std::optional<size_t> messagePlaceholder;
 qlie::Bytes eyecatchFallback;
 std::vector<qlie::Bytes>sounds;
 std::string path;
 mutable std::set<std::string>warnings;
 mutable std::map<size_t,std::unique_ptr<EmbeddedVideo>>clips;
 mutable std::map<size_t,Image>maskedImages;
 explicit Composite(qlie::Bytes bytes,const std::string&name,uint32_t&randomState,
                    bool deferred=false,EyecatchFilm* film=nullptr):path(name){
  auto nextId=[&]{return deferred?randomState++:particleRandomNext(randomState);};
  identity=nextId();
  if(kvideo_is_container(bytes.data(),bytes.size())){
   standaloneVideo=hasVideo=true;
   definition.bounds={};
   members.emplace_back();
   members.front().video=std::make_shared<const qlie::Bytes>(std::move(bytes));
   return;
  }
  bool metadata=false;
  for(auto&m:abmp(bytes)){
   if(m.type.rfind("abdata",0)==0){definition=animation(m.type,m.data);
 for(size_t i=0;i<definition.motions.size();i++)motionIdentities.push_back(nextId());
 metadata=true;
 }
   else if(m.type.rfind("abimg",0)==0){Element e;e.name=m.name;
 if(e.name.rfind("*Link,",0)==0){
  unsigned source=0;
  const auto end=e.name.data()+e.name.size();
  auto parsed=std::from_chars(e.name.data()+6,end,source);
  if(parsed.ec==std::errc{}&&parsed.ptr==end)linkedSlots.emplace_back(unsigned(members.size()),source);
 }
 if(e.name=="*MesSurface")messagePlaceholder=members.size();
 if(!m.data.empty()){
    if(m.data.size()>=5&&std::equal(m.data.begin(),m.data.begin()+5,"abmp1"))e.nested=std::make_shared<Composite>(std::move(m.data),name+"#"+m.name,randomState,deferred,film);
    else if(kvideo_is_container(m.data.data(),m.data.size())){e.video=std::make_shared<const qlie::Bytes>(std::move(m.data));
 }
    else if(m.data.size()>=12&&std::equal(m.data.begin(),m.data.begin()+12,"ARGBModel1.0"))e.mesh=std::make_shared<Model>(model(m.data));
    else if(m.data.size()>=8&&std::memcmp(m.data.data(),"IMOAVI\0\0",8)==0)e.jpegAnimation=std::make_shared<JpegAnimation>(std::move(m.data));
    // The story cards supply a portrait for slot zero. Keep the embedded
    // bitmap compressed until a caller draws the card without a replacement.
    else if(path==eyecatchPath&&members.empty())eyecatchFallback=std::move(m.data);
    else if(film&&path==eyecatchFilmPath&&m.name=="film")e.image=film->decode(std::move(m.data));
    else e.image=sceneBitmap(image(m.data));
   }
   hasVideo=hasVideo||bool(e.video)||(e.nested&&e.nested->hasVideo);
   members.push_back(std::move(e));
 }
   else if(m.type.rfind("absnd",0)==0)sounds.push_back(std::move(m.data));
  }
  if(!metadata)throw std::runtime_error("Composite lacks animation metadata "+name);
  for(auto&member:members)if(member.mesh){
   for(const auto&material:member.mesh->materials){
    auto picture=std::find_if(members.begin(),members.end(),[&](const auto&slot){
     return slot.image&&(slot.name==material.name||(!material.texture.empty()&&slot.name==material.texture));
    });
    size_t slot=picture==members.end()?SIZE_MAX:size_t(picture-members.begin());
    // Legacy embedded models can name an external texture in MQO and
    // supply its only image through a linked ABMP member.
    if(slot==SIZE_MAX&&!material.texture.empty()&&member.mesh->materials.size()==1&&linkedSlots.size()==1)
     slot=linkedSlots.front().first;
    auto channel=[&](unsigned index){
     float value;const auto bits=material.properties[index];
     std::memcpy(&value,&bits,sizeof(value));
     return unsigned(std::lround(std::clamp(value,0.f,1.f)*255));
    };
    member.materialDraw.push_back({slot,channel(2),(channel(3)<<16)|(channel(4)<<8)|channel(5)});
   }
  }
  bool wideKeys=false;
  for(const auto&motion:definition.motions)
   for(const auto&layer:motion.layers)wideKeys=wideKeys||layer.version>=21;
  if(wideKeys){
   std::istringstream comment(definition.comment);
   std::string line;
   while(std::getline(comment,line)){
    line=trim(std::move(line));
    if(line.rfind("*value[",0)!=0)continue;
    const auto end=line.find("]=",7);
    if(end==line.npos||end+2==line.size())continue;
    const auto index=std::stoul(line.substr(7,end-7));
    if(index<externalDefaults.size()){
     externalDefaults[index]=numeric(line.substr(end+2));
     externalDefaultMask|=uint16_t(1u<<index);
    }
   }
  }
  for(auto&motion:definition.motions)for(auto&layer:motion.layers){if(layer.mode>2)warn("layer-mode="+std::to_string(layer.mode));
 for(auto&k:layer.keys)if(k[4]!=0&&k[4]!=1&&k[4]!=2&&k[4]!=3&&k[4]!=4&&k[4]!=5&&k[4]!=6&&k[4]!=9&&k[4]!=10&&k[4]!=15&&k[4]!=16&&k[4]!=17&&k[4]!=18&&k[4]!=20)warn("primitive="+std::to_string(k[4]));
 }
 }
 size_t closeVideoClips(AssetReadAhead*worker){
  if(!hasVideo)return 0;
  size_t count=clips.size();
  // Each clip owns its compressed input, independent of the cached composite.
  // The renderer holds its own reference to any frame still submitted to the GPU.
  if(worker)for(auto&entry:clips){
   entry.second->cancel(worker);
   worker->retire(std::move(entry.second));
  }
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
  for(const auto&e:members){if(e.image)size+=e.image->pixels.size();if(e.nested)size+=e.nested->bytesHeld();if(e.video)size+=e.video->size();if(e.jpegAnimation)size+=e.jpegAnimation->data.size();}
  for(const auto&sound:sounds)size+=sound.size();
  for(const auto&clip:clips)if(clip.second->shown)size+=clip.second->shown->pixels.size();
  return size;
 }
 std::vector<PreparedEmbeddedOpening::Source> openingVideoSources()const{
  std::vector<PreparedEmbeddedOpening::Source> sources;
  if(standaloneVideo)return {{0,members.front().video,path}};
  if(definition.motions.empty())return sources;
  const auto&motion=definition.motions.front();
  AnimationContext context;
  context.external=externalDefaults;context.externalMask=externalDefaultMask;
  const auto keys=sampleMotion(motion,0,1280,720,false,context);
  for(size_t layer=0;layer<keys.size();++layer){
   const auto&key=keys[layer];
   if(!motion.layers[layer].flags||(key.kind!=3&&key.kind!=4)||key.alpha<=0||key.resource>=members.size())continue;
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
 EmbeddedVideo&videoClip(size_t index,const Element&element,AssetReadAhead*worker)const{
  auto&clip=clips[index];
  if(clip&&clip->decoder->bytes!=element.video){
   clip->cancel(worker);
   if(worker)worker->retire(std::move(clip));
   else clip.reset();
  }
  if(!clip)clip=std::make_unique<EmbeddedVideo>(element.video,standaloneVideo?path:path+"#"+element.name);
  return *clip;
 }
 std::pair<unsigned,unsigned> dimensions(AssetReadAhead*worker=nullptr,double ms=0,bool live=false,bool nativeVideo=false,bool async=true)const{
  if(standaloneVideo){
   auto&clip=videoClip(0,members.front(),worker);
   const auto frame=clip.at(ms,live,nativeVideo,true,worker,async);
   return frame?std::make_pair(frame->width,frame->height):std::make_pair(0u,0u);
  }
  return {unsigned(definition.bounds[0]),unsigned(definition.bounds[1])};
 }
 void enqueue(RenderQueue&queue,double ms,const std::string&motionName,const Affine2D&parent,float depth,float opacity,uint32_t parentAdd,uint32_t parentMul,unsigned parentBlend=0,const Image*replacement=nullptr,std::optional<const Image*>messageSurface=std::nullopt,unsigned windowAlpha=255,const std::map<unsigned,const Image*>*uiSlots=nullptr,const AnimationContext*external=nullptr,const Matrix3D*parent3D=nullptr,unsigned filterOverride=0,bool liveVideo=false,AssetReadAhead*worker=nullptr,bool asyncVideo=true,const Composite*replacementMedia=nullptr)const{
  if(standaloneVideo){
   if(opacity<=0)return;
   auto&clip=videoClip(0,members.front(),worker);
   auto image=clip.at(ms,liveVideo,queue.supportsVideoSurfaces(),true,worker,asyncVideo);
   if(!image)return;
   const auto&frame=queue.retainImage(std::move(image));
   if(clip.audible)warn("video nonzero audio requires mixer binding");
   if(parent3D){
    AnimationSample sample;sample.alpha=opacity;sample.addColor=parentAdd;sample.mulColor=parentMul;sample.flags=parentBlend;sample.textureFilter=filterOverride;
    quad(queue,frame,*parent3D,sample,depth);
   }else queue.image(frame,parent,depth,opacity,parentAdd,parentMul,parentBlend,filterOverride);
   return;
  }
  if(definition.bounds[0]<0||definition.bounds[1]<0||definition.bounds[0]>4096||definition.bounds[1]>4096)throw std::runtime_error("Composite bounds "+path);
  const Motion*motion=findMotion(definition,motionName);
 if(!motion)throw std::runtime_error("Missing motion "+motionName+" in "+path);
  auto randomSeed=0x35246u^identity^motionIdentities[size_t(motion-&definition.motions[0])];
  randomSeed=((randomSeed&0xffff)*(randomSeed>>16))&1023;
  AnimationContext context{unsigned(definition.bounds[0]),unsigned(definition.bounds[1]),1280,720,randomSeed};
  context.external=externalDefaults;context.externalMask=externalDefaultMask;
  if(external){
   for(unsigned i=0;i<context.external.size();++i)
    if(external->externalMask&(1u<<i))context.external[i]=external->external[i];
   context.externalMask|=external->externalMask;context.randomCursor=external->randomCursor;
  }
  auto samples=sampleMotion(*motion,ms,1280,720,false,context);
  auto bitmap=[&](size_t slot)->const Image*{
   if(slot>=members.size())return nullptr;
   if(uiSlots){
    const auto override=uiSlots->find(unsigned(slot));
    if(override!=uiSlots->end())return override->second;
   }
   if(slot==0&&replacement)return replacement;
   return members[slot].image.get();
  };
  auto childBitmaps=[&](const Composite&nested){
   std::map<unsigned,const Image*> result;
   for(auto [local,parentSlot]:nested.linkedSlots)if(const auto*image=bitmap(parentSlot))result.emplace(local,image);
   return result;
  };
  const size_t messageSlot=messagePlaceholder.value_or(1);
  auto replacementFrame=[&](double time){
   auto&clip=replacementMedia->videoClip(0,replacementMedia->members.front(),worker);
   auto frame=clip.at(time,liveVideo,queue.supportsVideoSurfaces(),false,worker,asyncVideo);
   if(clip.audible)warn("replacement video nonzero audio requires mixer binding");
   return frame?&queue.retainImage(std::move(frame)):nullptr;
  };
  for(size_t index=0;index<samples.size();index++){
 auto&k=samples[index];if(filterOverride)k.textureFilter=filterOverride;
 if(parentBlend)k.flags=parentBlend;
 for(auto&w:k.unsupported)if(w!="primitive=20"&&!(w=="3d-rotation"&&(k.kind==18||k.kind==2||k.kind==3||k.kind==4)))warn(w);
 if(!motion->layers[index].flags||!k.kind)continue;
 if(k.kind==5){queue.requestFieldPresentation();continue;}
 k.alpha=int(k.alpha)*int(opacity)/255;if(messageSurface.value_or(nullptr)&&messageSlot!=0&&k.kind==4&&k.resource==0)k.alpha=int(k.alpha)*int(windowAlpha)/255;k.addColor=colorAdd(k.addColor,parentAdd);k.mulColor=colorMultiply(k.mulColor,parentMul);
 if(k.kind==15||k.kind==16||k.kind==17){queue.control(k,depth+k.priority);continue;}
 if(k.kind==1){
  if(!supportsPixelBlend(k.flags&0x7f)){warn("rectangle-blend="+std::to_string(k.flags));continue;}
  if(std::abs(k.scaleX)>=32700||std::abs(k.scaleY)>=32700){warn("rectangle-symbolic-size");continue;}
  // PE eaff0/ea798: rectangle dimensions are pixels, not image scale percentages.
  static const Image white=[]{Image pixel{1,1,{255,255,255,255}};pixel.version.publish();return pixel;}();
  auto shape=imagePlacement(k.x,k.y,k.scaleX,k.scaleY,k.rotationZ,k.anchorX/100,k.anchorY/100,1,1);
  queue.image(white,affine(parent,shape),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);
  continue;
 }
 if(k.kind==9||k.kind==10){
  if(k.resource>=members.size()||k.childMotion>=members.size())throw std::runtime_error("Mask image index "+path);
  const bool textSource=messageSurface&&messagePlaceholder&&k.resource==messageSlot;
  const Image*source=textSource?*messageSurface:members[k.resource].image.get();
  const Image*mask=members[k.childMotion].image.get();
   if(uiSlots){auto src=uiSlots->find(k.resource);if(src!=uiSlots->end())source=src->second;auto msk=uiSlots->find(k.childMotion);if(msk!=uiSlots->end())mask=msk->second;}
  if(replacementMedia&&k.resource==0){source=replacementFrame(k.mediaTime);if(!source)continue;}
  if(textSource&&!source)continue;
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
   auto second=mask->pixels.data()+(size_t(my)*mask->width+mx)*4;
   if(k.mask.crossfade){
    // WHJ5 0x562164 picks the exact endpoints. 0x5385a4 mixes BGRA in 1/128 steps.
    const unsigned weight=k.mask.amount==255?128:k.mask.amount>>1;
    for(unsigned channel=0;channel<4;++channel)dst[channel]=uint8_t((src[channel]*(128-weight)+second[channel]*weight)>>7);
   }else{
    std::copy(src,src+4,dst);
    dst[3]=gradientMaskAlpha(src[3],second[0],k.mask.amount,k.mask.mode);
   }
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
   const auto resolved=childBitmaps(*e.nested);
   for(auto&point:particlePoints(k.particles,randomSeed,unsigned(e.nested->definition.motions.size()))){
    Matrix3D child;child.m[0][0]=child.m[1][1]=child.m[2][2]=point.scale;
    child.m[0][3]=point.x-e.nested->definition.bounds[0]*.5f;child.m[1][3]=point.y-e.nested->definition.bounds[1]*.5f;child.m[2][3]=point.z;
    child=combine(base,child);
    e.nested->enqueue(queue,nestedAnimationTime(e.nested->definition,point.milliseconds),e.nested->definition.motions.at(point.motion).name,{},depth+k.priority,k.alpha*point.alpha/255,k.addColor,k.mulColor,k.flags&0x7f,nullptr,std::nullopt,255,resolved.empty()?nullptr:&resolved,nullptr,&child,filterOverride,liveVideo,worker,false);
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
  const auto&element=members[k.resource];
  const auto&mesh=*element.mesh;
  auto matrix=combine(parent3D?*parent3D:from2D(parent),placement3D(k,0,0));
  std::vector<Vec3> transformed;
  for(const auto&object:mesh.objects)if(object.visible){
   transformed.clear();
   transformed.reserve(object.vertices.size());
   for(auto vertex:object.vertices)transformed.push_back(matrix.point(vertex));
   for(const auto&face:object.faces){
    if(face.material<0)continue;
    const auto index=unsigned(face.material);
    const auto&material=mesh.materials.at(index);
    const auto&draw=element.materialDraw.at(index);
    const auto*texture=bitmap(draw.slot);
    // PE df684 maps properties[0] to material+0xc; e0198 bypasses lighting for1.
    if(material.properties[0]!=1)warn("model-material-lighting="+std::to_string(material.properties[0]));
    if(!texture){warn("model-material-texture="+material.name);continue;}
    std::array<TextureVertex,4> vertices;for(unsigned i=0;i<face.corners;i++){const auto&point=transformed.at(face.vertices[i]);vertices[i]={point.x,point.y,1000-point.z,face.uv[i]*texture->width,face.uv[i+4]*texture->height};}
    queue.triangle(*texture,{vertices[0],vertices[1],vertices[2]},depth+k.priority,k.alpha*draw.alpha/255,k.addColor,colorMultiply(k.mulColor,draw.tint),k.flags&0x7f,k.textureFilter);
    if(face.corners==4)queue.triangle(*texture,{vertices[0],vertices[2],vertices[3]},depth+k.priority,k.alpha*draw.alpha/255,k.addColor,colorMultiply(k.mulColor,draw.tint),k.flags&0x7f,k.textureFilter);
   }
  }
  continue;
 }
 if(k.kind!=2&&k.kind!=3&&k.kind!=4)continue;
 if(k.resource>=members.size())throw std::runtime_error("Composite image index "+path);
   auto&e=members[k.resource];
 const bool textSlot=messageSurface&&k.resource==messageSlot;
 const Image*slot=textSlot?*messageSurface:k.resource==0?replacement:nullptr;
 if(uiSlots){auto override=uiSlots->find(k.resource);if(override!=uiSlots->end())slot=override->second;}
 if(replacementMedia&&k.resource==0){slot=replacementFrame(k.mediaTime);if(!slot)continue;}
 // A message window's authored text placeholder is never displayed while
 // its live text surface is pending. Keep actual animation images available.
 if(textSlot&&messagePlaceholder&&!slot)continue;
 const Image*source=slot?slot:e.image.get();
   if(slot&&k.kind==4){
    if(parent3D||k.rotationX||k.rotationY||k.z){auto matrix=combine(parent3D?*parent3D:from2D(parent),placement3D(k,source->width,source->height));quad(queue,*source,matrix,k,depth+k.priority);continue;}
    queue.image(*source,affine(parent,imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,source->width,source->height)),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,k.textureFilter);continue;
   }
   if(!slot&&e.nested){
 if(!k.childTimeResolved){warn("nested-time context unresolved");continue;}
 if(k.childMotion>=e.nested->definition.motions.size())throw std::runtime_error("Nested motion index "+path);
 AnimationContext nestedContext;nestedContext.randomCursor=context.randomCursor;
 const auto resolved=childBitmaps(*e.nested);
 const auto*slots=resolved.empty()?nullptr:&resolved;
 if(parent3D||k.rotationX||k.rotationY||k.z){
  auto matrix=combine(parent3D?*parent3D:from2D(parent),placement3D(k,unsigned(e.nested->definition.bounds[0]),unsigned(e.nested->definition.bounds[1])));
  e.nested->enqueue(queue,nestedAnimationTime(e.nested->definition,k.childTime),e.nested->definition.motions[k.childMotion].name,{},depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,nullptr,std::nullopt,255,slots,&nestedContext,&matrix,filterOverride,liveVideo,worker,asyncVideo);continue;
 }
 auto local=imagePlacement(k.x,k.y,k.scaleX/100,k.scaleY/100,k.rotationZ,k.anchorX/100,k.anchorY/100,unsigned(e.nested->definition.bounds[0]),unsigned(e.nested->definition.bounds[1]));
 e.nested->enqueue(queue,nestedAnimationTime(e.nested->definition,k.childTime),e.nested->definition.motions[k.childMotion].name,affine(parent,local),depth+k.priority,k.alpha,k.addColor,k.mulColor,k.flags&0x7f,nullptr,std::nullopt,255,slots,&nestedContext,nullptr,filterOverride,liveVideo,worker,asyncVideo);
 continue;
 }
   else if(!slot&&e.video){
    if(k.alpha<=0)continue;
    auto&clip=videoClip(index,e,worker);
    auto image=clip.at(k.mediaTime,liveVideo,queue.supportsVideoSurfaces(),false,worker,asyncVideo);
    if(!image)continue;
    source=&queue.retainImage(std::move(image));
    if(clip.audible)warn("video nonzero audio requires mixer binding");
   }else if(!slot&&e.jpegAnimation){
    if(k.alpha<=0)continue;
    source=&queue.retainImage(e.jpegAnimation->at(k.mediaTime));
   }
   // Primitive 3 specifies the rectangle in pixels, as in the original
   // 1280x720 noise layer driven by a 400x300 IMOAVI member.
   if(source&&k.kind==3){
    if(!source->width||!source->height)continue;
    k.scaleX=k.scaleX*100/source->width;k.scaleY=k.scaleY*100/source->height;
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
 if(!object)object=std::make_shared<Composite>(assets.read(path),path,particleState,false,
                                               path==eyecatchPath?eyecatchFilm.get():nullptr);
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
 auto film=path==eyecatchPath?eyecatchFilm:nullptr;
 readAhead->request(qlie::canonical(path),[&a=assets,path,prepareVideo,nativeVideo=prefetchNativeVideo,film=std::move(film)]{
  WarmAsset ready;auto bytes=a.read(path);
  if(sceneCompositePath(path)){
   ready.composite=std::make_shared<Composite>(std::move(bytes),path,ready.identities,true,film.get());
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
std::string SceneImages::filterName(const std::map<std::string,std::string>& params,unsigned slot){
 static constexpr const char*keys[]={"imgfilter0","imgfilter1","imgfilter2","imgfilter3"};
 const auto find=[&](const char*key){auto i=params.find(key);return i==params.end()?std::string{}:i->second;};
 auto base=find("imgfilterbase");
 auto filter=params.count(keys[slot])?find(keys[slot]):find("imgfilter");
 if(base=="none")base.clear();
 if(filter=="none")filter.clear();
 if(base.empty())return filter;
 return filter.empty()?base:base+"\\"+filter;
}
std::optional<SceneImages::BackgroundBlur> SceneImages::backgroundBlur(const std::string&line)const{
 if(!readAhead||line.rfind("^bg",0)!=0||line.find(",imgfilter")==line.npos||line.find("blur")==line.npos)return {};
 auto comma=line.find(',');
 auto layer=line.substr(1,comma-1);
 auto found=layers.find(layer);
 if(found==layers.end())return {};
 const auto&current=found->second;
 if(!current.pixels||current.composite)return {};
 auto params=current.params;
 // Only precompute filters that operate on the currently loaded bitmap.
 for(const auto&field:split(line.substr(comma+1),',')){
  auto colon=field.find(':');
  if(colon==field.npos)return {};
  auto name=field.substr(0,colon);
  if(name=="file"||name=="clear")return {};
  if(name.rfind("imgfilter",0)==0)params[name]=field.substr(colon+1);
 }
 auto filter=filterName(params,0);
 if(filter.find("blur")==filter.npos)return {};
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
 if(!chara&&!face&&!message&&cmd.rfind("bg",0)!=0&&cmd.rfind("effect",0)!=0&&!imageObjects.count(cmd))return result;
 auto layer=layers.find(cmd);
 const bool multipart=splitCharacters.count(cmd)!=0;
 std::string stem=chara&&layer!=layers.end()?layer->second.prefix:"";
 if(chara&&future){auto saved=future->simple.find(cmd);if(saved!=future->simple.end())stem=saved->second;}
 std::map<std::string,std::string> parts;
 if(multipart){
  if(future&&future->parts.count(cmd))parts=future->parts.at(cmd);
  else if(layer!=layers.end()){
   for(const auto&param:layer->second.params)if(characterPart(param.first))parts.insert(param);
  }
 }
 std::string base;
 // An authored template can supply the window, effect or actor's motion file.
 auto args=split(expand(line.substr(comma+1),multipart?"chara8e":chara?"chara":face?"face":message?"message":"image"),',');
 if(multipart){
  if(collectCharacterParts(args,parts))args.push_back("file:"+characterPartsName(parts));
  if(future)future->parts.insert_or_assign(cmd,std::move(parts));
 }
 for(auto&field:args){
  if(field=="clear:def"){base.clear();if(chara)stem.clear();}
  auto sep=field.find(':');if(sep==field.npos)continue;
  auto key=field.substr(0,sep),name=field.substr(sep+1);
  if(key=="file"&&!chara)name=split(name,':').front();
  if((chara||face)&&key=="base"){base=std::move(name);continue;}
  if(name.empty()||name=="none"||name=="none:none"||name.find('$')!=name.npos)continue;
  bool replacement=key.size()==4&&key.rfind("rep",0)==0;
  if(key!="file"&&!replacement)continue;
  std::string prefix="image/";
  if(!replacement){
   if(chara){prefix="chara/";name=characterImageName(name,stem);}
   else if(face)prefix="face/";else if(message)prefix=alternateMessageWindow?"avg2/window/":"avg/window/";
  }
  auto path=sceneResource(assets,prefix+name,true);
  if(path.empty())continue;
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
 if(multipart&&std::any_of(args.begin(),args.end(),[](const auto&arg){return arg.rfind("extmotion:",0)==0&&arg!="extmotion:def";})){
  std::string extension=layer!=layers.end()&&layer->second.params.count("extfile")?layer->second.params.at("extfile"):"";
  for(const auto&field:args)if(field.rfind("extfile:",0)==0)extension=field.substr(8);
  if(!extension.empty()&&extension!="none"){
   auto path=sceneResource(assets,"chara/"+extension,true);
   if(!path.empty()&&needsWarmFile(path,future!=nullptr))result.push_back(std::move(path));
  }
 }
 if(chara&&future)future->simple.insert_or_assign(cmd,std::move(stem));
 return result;
}
SceneImages::SceneImages(qlie::Assets&a):assets(a),eyecatchFilm(std::make_shared<EyecatchFilm>()){
 if(a.inPack("avg/projectobjects.txt")){
  std::istringstream objects(a.text("avg/projectobjects.txt"));
  std::string line;
  while(std::getline(objects,line)){
   const auto fields=split(line,',');
   if(fields.size()<2)continue;
   if(fields.front()=="chara8e")splitCharacters.insert(fields[1]);
   if(fields.front()=="image")imageObjects.insert(fields[1]);
   if(fields.front()=="camera"&&fields[1]=="camera")hasCamera=true;
   if(fields.front()=="nametext"&&fields[1]=="name")hasNameText=true;
  }
 }
 std::vector<const char*> templateTypes{"image","chara","face","message"};
 if(!splitCharacters.empty())templateTypes.push_back("chara8e");
 if(hasCamera)templateTypes.push_back("camera");
 if(hasNameText)templateTypes.push_back("nametext");
 for(auto type:templateTypes){std::istringstream in(a.text(std::string("avg/imoscripter_template_")+type+".txt"));
 std::string s;
 while(std::getline(in,s)){auto p=s.find(61);
 if(p!=s.npos)templates[type][trim(s.substr(0,p))]=trim(s.substr(p+1));
 }}
 auto table=[&](const std::string&path){std::map<std::string,std::string> out;std::istringstream in(a.text(path));std::string line;while(std::getline(in,line)){auto pos=line.find('=');if(pos!=line.npos)out[trim(line.substr(0,pos))]=trim(line.substr(pos+1));}return out;};
 faceRepId=table("avg/imoscripter_face_repid.txt");faceRepFile=table("avg/imoscripter_face_repfile.txt");if(a.exists("avg/imoscripter_generatorfilterlist.txt"))waveRecipes=table("avg/imoscripter_generatorfilterlist.txt");auto project=table("avg/projectsetting.txt");
 if(!splitCharacters.empty()&&a.exists("avg/imoscripter_extentionlist.txt"))
  extensionPositions=table("avg/imoscripter_extentionlist.txt");
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
void SceneImages::loadCharacterExtension(Layer&layer,const std::string&file){
 if(file.empty()||file=="none"){layer.extension.reset();return;}
 const auto path=sceneResource(assets,"chara/"+file,false);
 if(path.empty())throw std::runtime_error("Missing character extension "+file);
 layer.extension=sceneComposite(path);
}
void SceneImages::loadReplacement(Layer&layer,const std::string&path){
 layer.params["@replacement"]=path;
 layer.replacementVideo.reset();
 layer.scriptReplacement.reset();
 if(path.empty()){layer.scriptReplacement=std::make_shared<Image>();return;}
 if(const auto key=qlie::canonical(path);key.size()>=4&&key.compare(key.size()-4,4,".ogv")==0){
  layer.replacementVideo=sceneComposite(path);
  return;
 }
 auto at=cache.find(path);if(at==cache.end())at=cache.emplace(path,loadBitmap(path)).first;
 layer.scriptReplacement=at->second;
}
void SceneImages::prepareUiBitmap(const std::string&path){if(!cache.count(path))cache.emplace(path,loadBitmap(path));}
void SceneImages::warmUiBitmaps(const std::string&key,std::vector<std::string> paths){
 if(!readAhead)return;
 paths.erase(std::remove_if(paths.begin(),paths.end(),[&](const auto&path){return cache.count(path)>0;}),paths.end());
 if(paths.empty())return;
 readAhead->request(key,[&assetSource=assets,paths=std::move(paths)]{
  WarmAsset result;
  for(const auto&path:paths){
   auto bitmap=sceneBitmap(image(assetSource.read(path)));
   result.bytes+=bitmap->pixels.size();
   result.uiBitmaps.emplace(path,std::move(bitmap));
  }
  return result;
 });
}
void SceneImages::takeUiBitmaps(const std::string&key){
 if(readAhead)if(auto batch=readAhead->take(key))cache.merge(batch->uiBitmaps);
}
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
void SceneImages::uiExternal(const std::string&name,unsigned index,float v){
 if(name.rfind("ui_",0)!=0||index>=16||!std::isfinite(v))throw std::runtime_error("UI external variable");
 auto&layer=layers.at(name);
 auto&parameter=layer.params["@exv"+std::to_string(index)];
 parameter=std::to_string(v);
 layer.external[index]=numeric(parameter);
 layer.externalMask|=uint16_t(1u<<index);
}
void SceneImages::clearComposites(){
 if(readAhead)for(auto&entry:composites)if(entry.second->hasVideo){
  readAhead->discard(qlie::canonical(entry.first));
  entry.second->closeVideoClips(readAhead);
 }
 composites.clear();
}
void SceneImages::clear(bool keepBitmaps){
 const auto start=qlie::diagnostics?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
 const auto layerCount=layers.size(),bitmaps=cache.size(),animations=composites.size();
 layers.clear();
 if(!keepBitmaps)cache.clear();
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
 if(q<1)switch(t.mode){case 0:q=0;break;case 1:break;case 3:q=1-std::pow(1-q,exponent);break;case 4:q=std::pow(q,exponent);break;
  case 8:q=q<.5?.5*std::pow(q*2,exponent):1-.5*std::pow((1-q)*2,exponent);break;
  default:throw std::runtime_error("Unsupported image tween mode");}
 if(key=="mulcolor"||key=="addcolor"){uint32_t result=0;for(unsigned shift=0;shift<24;shift+=8){int from=(uint32_t(t.from)>>shift)&255,to=(uint32_t(t.to)>>shift)&255;result|=uint32_t(from+(to-from)*q)<<shift;}return float(result);}
 return float(t.from+(t.to-t.from)*q);
}
void SceneImages::transition(Layer&layer,const Layer&before,bool position,bool color){
 double duration=layer.params.count("time")?numeric(layer.params.at("time")):0;
 unsigned mode=1;auto m=layer.params.find("move");if(m!=layer.params.end()){
  if(m->second=="step")mode=0;else if(m->second=="linear")mode=1;else if(m->second=="outquart"||m->second=="square")mode=3;else if(m->second=="inquart"||m->second=="invsqare")mode=4;else if(m->second=="inoutquart")mode=8;
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
 if(layer.waveMilliseconds>=0)layer.waveMilliseconds+=ms;
 if(layer.extension)layer.extensionMilliseconds+=ms*std::max(0.,speed)/100;
 for(auto&t:layer.tweens)t.second.elapsed=std::min(t.second.duration,t.second.elapsed+ms);
 if(layer.params.count("@hiding")&&layer.params.at("@hiding")=="true"&&layer.composite){auto*motion=findMotion(layer.composite->definition,"*Disable");if(motion){auto samples=sampleMotion(*motion,layer.milliseconds);if(std::none_of(samples.begin(),samples.end(),[](auto&sample){return sample.kind!=0;})){layer.params["show"]="false";layer.params.erase("@hiding");}}}
 }
 // Keep loaded animation data available, but inactive composites no longer
 // need an open video decoder or its decoded frame.
 const auto start=qlie::diagnostics?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
 size_t closed=0;
 for(auto entry=composites.begin();entry!=composites.end();){
  auto&object=entry->second;
  if(object.use_count()==1){
   closed+=object->closeVideoClips(readAhead);
   if(object->standaloneVideo){
    if(readAhead)readAhead->retire(std::move(object));
    entry=composites.erase(entry);
    continue;
   }
  }
  ++entry;
 }
 if(qlie::diagnostics&&closed){
  const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(ms>=1000.0/60)QLIE_LOGF("LOAD video_cleanup_ms=%.3f clips=%zu\n",ms,closed);
 }
}
void SceneImages::setWave(Layer&layer,std::string recipe){
 if(recipe.size()>=2&&recipe.front()=='"'&&recipe.back()=='"')recipe=recipe.substr(1,recipe.size()-2);
 const auto named=waveRecipes.find(recipe);
 if(named!=waveRecipes.end())recipe=named->second;
 for(auto i=layer.params.lower_bound("wave_");i!=layer.params.end()&&i->first.rfind("wave_",0)==0;)
  i=layer.params.erase(i);
 layer.waveMilliseconds=-1;
 if(recipe=="none"||recipe.empty())return;
 for(const auto&item:split(recipe,',')){
  const auto separator=item.find(':');
  if(separator!=item.npos)layer.params["wave_"+item.substr(0,separator)]=item.substr(separator+1);
 }
 if(layer.params.count("wave_mode"))layer.waveMilliseconds=0;
}
std::array<float,3> SceneImages::sampleWave(const Layer&layer){
 const auto mode=layer.params.find("wave_mode");
 if(mode==layer.params.end()||mode->second=="none")return {};
 const double time=(layer.waveMilliseconds>=0?layer.waveMilliseconds:layer.milliseconds)-value(layer,"wave_delay",0);
 const double duration=value(layer,"wave_time",0);
 const bool repeat=layer.params.count("wave_infinity")&&layer.params.at("wave_infinity")=="true";
 if(time<0||(!repeat&&time>=duration))return {};
 const auto phase=time/std::max(1.f,value(layer,"wave_cycle",100));
 const auto fade=layer.params.find("wave_fade");
 const auto elapsed=std::clamp(time/std::max(1.,duration),0.,1.);
 const double envelope=fade==layer.params.end()?1:fade->second=="in"?elapsed:fade->second=="out"?1-elapsed:1;
 const auto amplitude=[&](unsigned axis){
  if(mode->second=="sin"||mode->second=="abssin"){
   constexpr double tau=6.283185307179586;
   const auto sine=std::sin(phase*tau);
   return mode->second=="abssin"?std::abs(sine):sine;
  }
  static const auto random=particleRandomTable();
  const auto step=unsigned(std::fmod(std::floor(phase),1024.));
  const auto sample=[&](unsigned offset){return double(random[(step+axis*239+offset)&1023]&0xffff)/32767.5-1;};
  if(mode->second=="randombezier"){
   const auto u=phase-std::floor(phase),t=u*u*(3-2*u);
   return sample(0)+(sample(1)-sample(0))*t;
  }
  return sample(0);
 };
 return {float(amplitude(0)*envelope*value(layer,"wave_x",0)),
         float(amplitude(1)*envelope*value(layer,"wave_y",0)),
         float(amplitude(2)*envelope*value(layer,"wave_z",0))};
}
bool SceneImages::cameraCommand(const std::string&arguments){
 const auto resetCamera=[](Layer&layer){
  layer=Layer{};
  layer.params={{"show","false"},{"x","0"},{"y","0"},{"z","0"},{"scalex","100"},{"scaley","100"},
   {"rotz","0"},{"pri1","-100"},{"pri2","999"},{"move","linear"},{"time","0"}};
 };
 auto&camera=layers["camera"];
 if(camera.params.empty())resetCamera(camera);
 auto before=camera;
 const char*sourceKeys[]={"ax","ay","az","zoomx","zoomy","rotz"};
 const char*transformKeys[]={"x","y","z","scalex","scaley","rotz"};
 auto duration=camera.params["time"];
 bool moved=false;
 for(const auto&field:split(expand(arguments,"camera"),',')){
  const auto colon=field.find(':');
  if(colon==field.npos)throw std::runtime_error("Unknown camera template "+field);
  const auto key=field.substr(0,colon),value=field.substr(colon+1);
  if(key=="clear"&&value=="def"){
   resetCamera(camera);
   before=camera;
   duration="0";
   moved=false;
   continue;
  }
  if(key=="genfilter"){setWave(camera,value);continue;}
  if(key=="time")camera.params[key]=duration=value;
  if(key=="movetime")duration=value;
  if(key=="pri1"||key=="pri2"||key=="move"||key=="movepow")camera.params[key]=value;
  for(unsigned i=0;i<6;++i){
   if(key==sourceKeys[i]){camera.params[transformKeys[i]]=value;moved=true;}
   if(key=="b"+std::string(sourceKeys[i])){before.params[transformKeys[i]]=value;before.tweens.erase(transformKeys[i]);moved=true;}
  }
 }
 if(moved){
  const auto baseTime=camera.params["time"];
  camera.params["time"]=duration.empty()?"0":duration;
  transition(camera,before,true,false);
  camera.params["time"]=baseTime.empty()?"0":baseTime;
 }
 return true;
}
SpeakerStyle SceneImages::speakerStyle()const{
 const auto found=layers.find("name");
 if(found==layers.end())return {};
 const auto&layer=found->second;
 return {int(value(layer,"x",200)),int(value(layer,"y",570)),
         unsigned(value(layer,"fontsizeh",26)),unsigned(value(layer,"fontcolor",0xffffff))};
}
bool SceneImages::command(const std::string&line){auto p=line.find(44);
 auto cmd=line.substr(1,p-1);
 if(p!=line.npos&&hasCamera&&cmd=="camera")return cameraCommand(line.substr(p+1));
 if(p!=line.npos&&hasNameText&&cmd=="name"){
  auto&name=layers[cmd];
  name.params["show"]="false";
  for(const auto&field:split(expand(line.substr(p+1),"nametext"),',')){
   const auto colon=field.find(':');
   if(colon==field.npos)throw std::runtime_error("Unknown name template "+field);
   name.params[field.substr(0,colon)]=field.substr(colon+1);
  }
  return true;
 }
 bool chara=cmd.rfind("chara",0)==0,face=cmd=="face",message=cmd=="message",ui=cmd.rfind("ui_",0)==0;
 if(!ui&&!chara&&!face&&!message&&cmd.rfind("bg",0)!=0&&cmd!="effect"&&cmd!="env"&&!imageObjects.count(cmd))return false;
 const bool multipart=splitCharacters.count(cmd)!=0;
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
 auto resetsMotion=[&](const char*key,const char*keep){
  const auto motion=l.params.find(key);
  return motion!=l.params.end()&&motion->second!="def"&&motion->second!="0"&&
   motion->second.find('+')==std::string::npos&&motion->second.find("＋")==std::string::npos&&
   (!l.params.count(keep)||l.params.at(keep)!="true");
 };
 if(chara&&resetsMotion("motion","motionkeep")){
  l.params["motion"]="def";l.milliseconds=0;l.audioMilliseconds=-1;
 }
 if(multipart&&resetsMotion("extmotion","extmotionkeep"))l.params["extmotion"]="def";
 auto args=split(expand(p==line.npos?"":line.substr(p+1),multipart?"chara8e":chara?"chara":face?"face":message?"message":"image"),44);
 if(multipart){
  std::map<std::string,std::string> parts;
  for(const auto&param:l.params)if(characterPart(param.first))parts.insert(param);
  if(collectCharacterParts(args,parts)){
   const auto name=characterPartsName(parts);
   for(unsigned i=0;i<8;++i)l.params.erase("file"+std::to_string(i));
   args.push_back("file:"+name);
   for(const auto&part:parts)args.push_back(part.first+":"+part.second);
  }
 }
 Layer before=l;bool file=false,baseChanged=false,position=false,color=false,messageShow=false;bool replacementChanged[4]={};
 std::string colorTime,moveTime;
 std::map<std::string,std::string> beginPosition;
 if(std::any_of(args.begin(),args.end(),[](const std::string&arg){return arg.rfind("file:",0)==0;})){
  l.replacementVideo.reset();l.scriptReplacement.reset();l.params.erase("@replacement");for(unsigned i=0;i<4;i++)l.params.erase("rep"+std::to_string(i));
 }
 for(auto&s:args){if(s.empty())continue;
 auto c=s.find(58);
 if(c==s.npos)throw std::runtime_error("Unknown image template "+s);
 auto key=s.substr(0,c),v=s.substr(c+1);
 // Imo_Image_Excute keeps only the first value after the property name.
 if(key=="file"&&!chara)v=split(v,':').front();
 if(multipart&&characterPart(key)){
  if(v=="none")l.params.erase(key);
  else l.params[key]=v;
  continue;
 }
 if(key.size()==4&&key.rfind("rep",0)==0&&key[3]>='0'&&key[3]<='3')replacementChanged[key[3]-'0']=true;
 if(key=="clear"&&v=="def"&&!face&&!ui){
  l=Layer{};
  l.params=message?messageDefaults:chara?charaDefaults:imageDefaults;
  file=baseChanged=position=color=messageShow=false;
  colorTime.clear();moveTime.clear();beginPosition.clear();
  std::fill(std::begin(replacementChanged),std::end(replacementChanged),false);continue;
 }
 if(face&&key=="clear"&&v=="def"){l=Layer{};l.params=faceDefaults;continue;}
 if(key=="imgfilter"&&(!l.params.count(key)||l.params.at(key)!=v))l.filtered={};
 if(key=="colortime"||key=="movetime"||key=="oncetime"){
  if(key=="colortime"||(key=="oncetime"&&colorTime.empty()))colorTime=v;
  if(key=="movetime"||(key=="oncetime"&&moveTime.empty()))moveTime=v;
  continue;
 }
 if(key.size()==6&&key.rfind("value",0)==0&&key.back()>='0'&&key.back()<='9'){
  const unsigned index=key.back()-'0';
  l.params["@exv"+std::to_string(index)]=v;
  l.external[index]=numeric(v);l.externalMask|=uint16_t(1u<<index);
  continue;
 }
 if(!key.empty()&&key.front()=='b'){
  static const std::set<std::string> startKeys{"bx","by","bz","bax","bay","baz",
   "bscalex","bscaley","bscalez","brotx","broty","brotz","bpri"};
  if(startKeys.count(key)){beginPosition[key.substr(1)]=v;position=true;continue;}
 }
 if(chara&&key=="motionkeep"){l.params[key]=v;continue;}
 if(multipart&&(key=="hide"||key=="extfile"||key=="extmotion"||key=="extmotionkeep"||
                  key=="extax"||key=="extay"||key=="extaz"||key=="extsx"||key=="extsy")){
  l.params[key]=v;
  if(key=="extfile"){l.extension.reset();l.params["extmotion"]="def";l.params["extmotionkeep"]="false";}
  if(key=="extmotion"){l.extensionMilliseconds=0;l.params["extmotionkeep"]="false";}
  continue;
 }
 if(key=="balpha"||key=="baddcolor"||key=="bmulcolor"){
  before.params[key.substr(1)]=v;
  before.tweens.erase(key.substr(1));
  color=true;
  continue;
 }
 if(key=="genfilter"){setWave(l,v);continue;}
 if(key=="imgfilterbase"){
  l.params[key]=v;
  continue;
 }
 if(key=="imgfilter0"||key=="imgfilter1"||key=="imgfilter2"||key=="imgfilter3"||key=="blend"){
  l.params[key]=v;
  continue;
 }
 if(key=="clear"){l.filtered={};l.params.erase("@portrait");l.params.erase("@replacement");l.replacementVideo.reset();l.scriptReplacement.reset();l.path.clear();
 l.pixels.reset();
 l.composite.reset();
 l.params["show"]="false";
 continue;
 }
 if(message&&(key=="textareaw"||key=="textareah"||key=="fontsizew"||key=="fontsizeh")){
  const auto pairKey=key.substr(0,key.size()-1);
  auto pair=split(l.params.at(pairKey),':');
  if(pair.size()==1)pair.push_back(pair.front());
  pair[key.back()=='h']=v;
  l.params[pairKey]=pair[0]+":"+pair[1];
  continue;
 }
 l.params[key]=v;
 if(message&&key=="show")messageShow=true;
 if(key=="file"){l.filtered={};file=true;if(face)l.params["@face-active"]="true";}
 if((chara||face)&&key=="base")baseChanged=true;
 if(key=="x"||key=="y"||key=="z"||key=="ax"||key=="ay"||key=="az"||key=="scalex"||key=="scaley"||key=="scalez"||key=="rotx"||key=="roty"||key=="rotz"||key=="pri")position=true;
 if(key=="alpha"||key=="mulcolor"||key=="addcolor")color=true;
 if(key=="vol"&&std::stof(v)==0)l.audioStopPending=true;
 if(key=="motion"){l.milliseconds=0;l.audioMilliseconds=-1;if(chara)l.params["motionkeep"]="false";}
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
   path=sceneResource(assets,"image/"+replacement,false);
   if(path.empty())throw std::runtime_error("Missing replacement image "+replacement);
  }loadReplacement(l,path);
 }
 if(!beginPosition.empty()){
  for(const auto&[key,val]:beginPosition){
   before.params[key]=val;before.tweens.erase(key);
  }
  constexpr const char*base[]={"x","y","z"};
  constexpr const char*offset[]={"ax","ay","az"};
  constexpr float defaults[]={640,360,0};
  for(unsigned axis=0;axis<3;++axis){
   const auto current=beginPosition.find(base[axis]),delta=beginPosition.find(offset[axis]);
   if(current==beginPosition.end()&&delta==beginPosition.end())continue;
   const auto existing=l.params.find(base[axis]);
   const auto existingOffset=l.params.find(offset[axis]);
   const float from=current!=beginPosition.end()?numeric(current->second):existing!=l.params.end()?numeric(existing->second):defaults[axis];
   const float step=delta!=beginPosition.end()?numeric(delta->second):existingOffset!=l.params.end()?numeric(existingOffset->second):0;
   before.params[base[axis]]=std::to_string(from+step);
   before.tweens.erase(base[axis]);
  }
 }
 if(multipart&&l.params.count("extfile")&&l.params.count("extmotion")&&
    l.params.at("extmotion")!="def"&&!l.extension)
  loadCharacterExtension(l,l.params.at("extfile"));
 if(position&&!moveTime.empty()){
  const auto time=l.params.find("time");
  const std::optional<std::string> original=time==l.params.end()?std::nullopt:std::optional<std::string>(time->second);
  l.params["time"]=moveTime;
  transition(l,before,true,false);
  if(original)l.params["time"]=*original;else l.params.erase("time");
  position=false;
 }
 transition(l,before,position,color&&colorTime.empty());
 if(color&&!colorTime.empty()){
  const auto duration=l.params.find("time");
  const auto originalTime=duration==l.params.end()?std::string{}:duration->second;
  l.params["time"]=colorTime;
  transition(l,before,false,true);
  if(originalTime.empty())l.params.erase("time");
  else l.params["time"]=originalTime;
 }
 auto loadBase=[&](){auto base=l.params.find("base");if((!chara&&!face)||base==l.params.end()||base->second=="none"||base->second.empty())return false;
  std::string path=(face?"face/":"chara/")+base->second;if(!assets.exists(path)&&assets.exists(path+".b"))path+=".b";
  if(!assets.exists(path))throw std::runtime_error("Missing character base "+path);
  auto loaded=sceneComposite(path);
  const bool preserve=loaded==l.composite&&l.params.count("motionkeep")&&l.params.at("motionkeep")=="true";
  l.composite=std::move(loaded);l.path=path;
  if(!preserve){l.milliseconds=0;l.audioMilliseconds=-1;}
  return true;
 };
 if(baseChanged&&!file){if(!loadBase()){l.composite.reset();auto portrait=l.params.find("@portrait");l.path=portrait==l.params.end()?"":portrait->second;}return true;}
 if(file){std::string f=l.params["file"];
 if(f=="none"||f=="none:none"){l.params.erase("@portrait");l.params.erase("@replacement");l.replacementVideo.reset();l.scriptReplacement.reset();l.path.clear();
 l.pixels.reset();
 l.composite.reset();
 return true;
 }if(chara)f=characterImageName(f,l.prefix);
 const std::string requested=(ui?"":chara?"chara/":face?"face/":message?(alternateMessageWindow?"avg2/window/":"avg/window/"):"image/")+f;
 auto path=sceneResource(assets,requested,false);
 if(path.empty())throw std::runtime_error("Missing image "+requested);
 l.path=path;
 if(chara||face)l.params["@portrait"]=path;
 if(sceneCompositePath(path)){l.pixels.reset();
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
std::pair<unsigned,unsigned> SceneImages::dimensions(const std::string&name)const{const auto&layer=layers.at(name);if(layer.composite){
 if(layer.composite->standaloneVideo){
  const auto&clip=layer.composite->videoClip(0,layer.composite->members.front(),readAhead);
  if(clip.shown)return {clip.shown->width,clip.shown->height};
  return layer.composite->dimensions(readAhead,layer.milliseconds,false,prefetchNativeVideo);
 }
 return layer.composite->dimensions();
}if(layer.pixels)return {layer.pixels->width,layer.pixels->height};throw std::runtime_error("Image dimensions unavailable: "+name);}
bool SceneImages::draw(qlie::Bytes&out,bool liveVideo,const Affine2D*view)const{return render(out,0,nullptr,999,liveVideo,view);}
void SceneImages::drawThumbnail(qlie::Bytes&out,float maxPriority)const{render(out,4,nullptr,maxPriority);}
bool SceneImages::drawScene(qlie::Bytes&out,bool liveVideo)const{return render(out,1,nullptr,999,liveVideo);}
void SceneImages::drawFaces(qlie::Bytes&out)const{render(out,2);}
void SceneImages::drawMessages(qlie::Bytes&out)const{render(out,3);}
void SceneImages::setViewPan(std::string layer,float horizontal,float vertical){
 panLayer=std::move(layer);
 panExtentX=horizontal;panExtentY=vertical;
 panX=panY=0;
}
void SceneImages::moveViewPan(float dx,float dy){
 panX=std::clamp(panX+dx,-1.f,1.f);
 panY=std::clamp(panY+dy,-1.f,1.f);
}
void SceneImages::pointViewPan(float x,float y){
 panX=std::clamp((x-640)/640,-1.f,1.f);
 panY=std::clamp((y-360)/360,-1.f,1.f);
}
MessageStyle SceneImages::messageStyle()const{MessageStyle style;auto it=layers.find("message");if(it==layers.end())return style;auto&params=it->second.params;
 auto pair=[&](const std::string&key,unsigned&a,unsigned&b){auto found=params.find(key);if(found==params.end())return;auto values=split(found->second,':');a=unsigned(numeric(values[0]));b=values.size()>1?unsigned(numeric(values[1])):a;};
 pair("textarea",style.width,style.height);pair("fontsize",style.fontWidth,style.fontHeight);
auto color=params.find("fontcolor");if(color!=params.end())style.color=uint32_t(numeric(color->second));return style;
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
bool SceneImages::render(qlie::Bytes&out,unsigned pass,RenderTarget*target,float maxPriority,bool liveVideo,const Affine2D*view)const{
 if(!target&&out.size()!=1280*720*4)throw std::runtime_error("Scene canvas size");
 RenderQueue queue(target&&target->supportsMasks(),target&&target->supportsVideoSurfaces(),view);
 enqueue(queue,pass,maxPriority,liveVideo);
 if(target)queue.draw(*target,1280,720);else queue.draw(out,1280,720);
 return queue.usesFieldPresentation();
}
void SceneImages::enqueue(RenderQueue&queue,unsigned pass,float maxPriority,bool liveVideo)const{
 const auto priority=[](const Layer&layer){return value(layer,"pri",0)+value(layer,"addpri",0);};
 const auto camera=layers.find("camera");
 Affine2D view;
 float nearPriority=1,farPriority=0,viewZ=0;
 if(camera!=layers.end()){
  const auto&layer=camera->second;
  float x=value(layer,"x",0),y=value(layer,"y",0);
  viewZ=value(layer,"z",0);
  float zoomX=value(layer,"scalex",100),zoomY=value(layer,"scaley",100);
  const auto wave=sampleWave(layer);
  if(layer.params.count("wave_target")&&layer.params.at("wave_target")=="zoom"){
   zoomX+=wave[0];zoomY+=wave[1];
  }else{
   x+=wave[0];y+=wave[1];viewZ+=wave[2];
  }
  view=imagePlacement(640+x,360+y,zoomX/100,zoomY/100,value(layer,"rotz",0),.5f,.5f,1280,720);
  nearPriority=value(layer,"pri1",-100);farPriority=value(layer,"pri2",999);
 }
 std::vector<const Layer*>sorted;
 for(auto&x:layers){auto&layer=x.second;bool face=x.first=="face",message=x.first=="message";
 if((pass==1&&(face||message))||(pass==2&&!face)||(pass==3&&!message)||(pass==4&&priority(layer)>maxPriority))continue;
 if(face&&layer.params.at("disp")!="keep"&&layer.params.at("@face-active")!="true"){
 if(layer.params.at("show")=="true"&&layer.params.at("mode")=="auto"&&!faceWarning){QLIE_LOG_LINE(<<"PENDING-FACE automatic portrait selection\n");faceWarning=true;}continue;
 }
 auto base=layer.params.find("base");
 if(base!=layer.params.end()&&!base->second.empty()&&base->second!="none"&&!layer.params.count("@portrait"))continue;
 if((layer.pixels||layer.composite)&&layer.params.at("show")!="false"&&
    (!layer.params.count("hide")||layer.params.at("hide")!="true"))sorted.push_back(&layer);
 }
 auto get=[&](const Layer*l,const std::string&k,float def){return k=="pri"?priority(*l):value(*l,k,def);};
 std::stable_sort(sorted.begin(),sorted.end(),[&](auto*a,auto*b){return get(a,"pri",0)<get(b,"pri",0);});
 const auto panned=layers.find(panLayer);
 for(auto*l:sorted){
 auto beforeImages=queue.imageCount();
 auto align=l->params.at("align");
 if(l->params.count("alignx")||l->params.count("aligny")){
  const auto pieces=split(align,':');
  const auto x=l->params.count("alignx")?l->params.at("alignx"):pieces[0];
  const auto y=l->params.count("aligny")?l->params.at("aligny"):pieces.size()>1?pieces[1]:"top";
  align=x+":"+y;
 }
 float ax=align.rfind("center",0)==0?0.5f:align.rfind("right",0)==0?1.f:0.f,ay=align.find(":bottom")!=align.npos?1.f:align.find(":center")!=align.npos?0.5f:0.f;
 // Shared composite instances can be sampled at different times in one frame.
 // Keep those ordered on the caller, and queue ordinary single-owner playback.
 const bool asyncVideo=l->composite&&l->composite.use_count()<=2&&
  (!l->replacementVideo||l->replacementVideo.use_count()<=2);
 const auto [w,h]=l->composite?l->composite->dimensions(readAhead,l->milliseconds,liveVideo,queue.supportsVideoSurfaces(),asyncVideo):std::make_pair(l->pixels->width,l->pixels->height);
 if(l->composite&&l->composite->standaloneVideo&&(!w||!h))continue;
 const auto blend=l->params.find("blend");
 const unsigned blendMode=blend==l->params.end()?0:imageBlend(blend->second);
 float layerX=get(l,"x",640),layerY=get(l,"y",360);
 float layerZ=get(l,"z",0),scaleX=get(l,"scalex",100),scaleY=get(l,"scaley",100),scaleZ=get(l,"scalez",100);
 if(panned!=layers.end()&&l==&panned->second){
  layerX=640-panExtentX*std::clamp(1.7f*panX,-1.f,1.f);
  layerY=360-panExtentY*std::clamp(1.7f*panY,-1.f,1.f);
 }
 const auto wave=sampleWave(*l);
 if(l->params.count("wave_target")&&l->params.at("wave_target")=="zoom"){
  scaleX+=wave[0];scaleY+=wave[1];scaleZ+=wave[2];
 }else{
  layerX+=wave[0];layerY+=wave[1];layerZ+=wave[2];
 }
 auto placement=imagePlacement(layerX,layerY,scaleX/100,scaleY/100,get(l,"rotz",0),ax,ay,w,h);
 AnimationSample transform;transform.x=layerX;transform.y=layerY;transform.z=layerZ;transform.scaleX=scaleX;transform.scaleY=scaleY;transform.scaleZ=scaleZ;transform.rotationX=get(l,"rotx",0);transform.rotationY=get(l,"roty",0);transform.rotationZ=get(l,"rotz",0);transform.anchorX=ax*100;transform.anchorY=ay*100;
 const auto depth=priority(*l);
 const bool cameraAffects=depth>=nearPriority&&depth<=farPriority;
 if(cameraAffects){
  placement=affine(view,placement);
  transform.z+=viewZ;
 }
 bool spatial=transform.z!=0||transform.rotationX!=0||transform.rotationY!=0||transform.scaleZ!=100;
 Matrix3D matrix;if(spatial)matrix=placement3D(transform,w,h);
 if(spatial&&cameraAffects)matrix=combine(from2D(view),matrix);
 std::map<unsigned,const Image*> uiSlots;for(auto&slot:l->uiSlots)uiSlots[slot.first]=slot.second.get();if(l->scriptReplacement)uiSlots[0]=l->scriptReplacement.get();
 if(l->path==eyecatchPath&&l->composite&&!uiSlots.count(0))
  l->composite->loadEyecatchFallback();
 auto filtered=[&](unsigned slot,const std::shared_ptr<const Image>&source,const std::string&filter)->const Image*{
  auto&cached=l->filtered[slot];if(filter.empty()){cached={};return source.get();}
  if(cached.source!=source||cached.name!=filter){auto result=sceneBitmap(filterImage(*source,filter));cached.source=source;cached.name=filter;cached.result=std::move(result);}
  return cached.result.get();
 };
 const Image*plain=l->pixels.get();
 const bool messageLayer=layers.count("message")&&l==&layers.at("message");
 if(l->composite){for(unsigned i=0;i<4;i++){
  const auto filter=filterName(l->params,i);
  if(filter.empty()){l->filtered[i]={};continue;}
  std::shared_ptr<const Image> source=i<l->composite->members.size()?l->composite->members[i].image:nullptr;
  if(i==0&&l->params.count("@portrait"))source=l->pixels;
  if(messageLayer&&i==l->composite->messagePlaceholder.value_or(1))source=l->pixels;
  auto slot=l->uiSlots.find(i);if(slot!=l->uiSlots.end())source=slot->second;
  if(i==0&&l->scriptReplacement)source=l->scriptReplacement;
  if(source)uiSlots[i]=filtered(i,source,filter);
  else if(i<l->composite->members.size()){auto&e=l->composite->members[i];if(e.nested||e.mesh||e.video)l->composite->warn("image-filter-nonbitmap-slot="+std::to_string(i));}
 }}else if(l->pixels)plain=filtered(0,l->pixels,filterName(l->params,0));
 AnimationContext external;external.randomCursor=&waveformCursor;external.external=l->external;external.externalMask=l->externalMask;
 if(l->composite){auto m=l->params.find("motion");
  std::optional<const Image*> messageSurface;
  if(messageLayer)messageSurface=l->pixels.get();
  l->composite->enqueue(queue,l->milliseconds,m==l->params.end()?"":m->second,placement,get(l,"pri",0),get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)),blendMode,l->params.count("@portrait")?l->pixels.get():nullptr,messageSurface,windowAlpha,uiSlots.empty()?nullptr:&uiSlots,&external,spatial?&matrix:nullptr,unsigned(get(l,"texfilter",0)),liveVideo,readAhead,asyncVideo,l->replacementVideo.get());
 }
 else if(spatial){transform.alpha=get(l,"alpha",255);transform.addColor=uint32_t(get(l,"addcolor",0));transform.mulColor=uint32_t(get(l,"mulcolor",0xffffff));transform.flags=blendMode;transform.textureFilter=unsigned(get(l,"texfilter",0));quad(queue,*plain,matrix,transform,get(l,"pri",0));}
 else queue.image(*plain,placement,get(l,"pri",0),get(l,"alpha",255),uint32_t(get(l,"addcolor",0)),uint32_t(get(l,"mulcolor",0xffffff)),blendMode,unsigned(get(l,"texfilter",0)));
 if(l->extension){
  const auto&motion=l->params.at("extmotion");
  if(motion!="def"){
   auto dimensions=l->extension->dimensions(readAhead,l->extensionMilliseconds,liveVideo,queue.supportsVideoSurfaces(),false);
   const auto file=l->params.find("@portrait");
   auto portrait=file==l->params.end()?std::string{}:file->second.substr(file->second.find_last_of('/')+1);
   if(const auto dot=portrait.find_last_of('.');dot!=portrait.npos)portrait.resize(dot);
   auto position=extensionPositions.find(l->params.at("extfile")+"_"+motion+"_"+portrait);
   if(position==extensionPositions.end())position=extensionPositions.find("default");
   float x=0,y=0,z=0,sx=100,sy=100;
   if(position!=extensionPositions.end()){
    const auto fields=split(position->second,',');
    if(fields.size()>0&&!fields[0].empty())x=numeric(fields[0]);
    if(fields.size()>1&&!fields[1].empty())y=numeric(fields[1]);
    if(fields.size()>2&&!fields[2].empty())z=numeric(fields[2]);
    if(fields.size()>3&&!fields[3].empty()&&numeric(fields[3])!=0)sx=numeric(fields[3]);
    if(fields.size()>4&&!fields[4].empty()&&numeric(fields[4])!=0)sy=numeric(fields[4]);
   }
   x=layerX+(x+get(l,"extax",0))*scaleX/100;
   y=layerY+(y+get(l,"extay",0))*scaleY/100;
   z=layerZ+(z+get(l,"extaz",0))*scaleZ/100+(cameraAffects?viewZ:0);
   const float ex=sx*get(l,"extsx",100)*scaleX/10000,ey=sy*get(l,"extsy",100)*scaleY/10000;
   auto fx=imagePlacement(x,y,ex/100,ey/100,0,.5f,.5f,dimensions.first,dimensions.second);
   Matrix3D extended;
   if(z!=0){
    AnimationSample sample;sample.x=x;sample.y=y;sample.z=z;sample.scaleX=ex;sample.scaleY=ey;sample.scaleZ=scaleZ;
    extended=placement3D(sample,dimensions.first,dimensions.second);
    if(cameraAffects)extended=combine(from2D(view),extended);
   }
   if(cameraAffects)fx=affine(view,fx);
   l->extension->enqueue(queue,l->extensionMilliseconds,motion,fx,depth+10,255,0,0xffffff,0,
       nullptr,std::nullopt,255,nullptr,nullptr,z!=0?&extended:nullptr,0,liveVideo,readAhead,false);
  }
 }
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
 const bool activeExtension=l.extension&&l.params.at("extmotion")!="def";
 w.n(l.params.size()+(l.waveMilliseconds>=0)+activeExtension);
 if(activeExtension){w.str("@extension-clock");w.str(std::to_string(uint64_t(l.extensionMilliseconds*1000)));}
 if(l.waveMilliseconds>=0){w.str("@wave-clock");w.str(std::to_string(uint64_t(l.waveMilliseconds*1000)));}
 for(auto&p:l.params){w.str(p.first);
 w.str(p.second);
 }if(l.composite)l.composite->saveIdentity(w);
 }return w.bytes;
 }
SceneImages::SceneImages(const SceneImages&live,RestoreTarget)
 :windowAlpha(live.windowAlpha),particleState(live.particleState),
  faceRepId(live.faceRepId),faceRepFile(live.faceRepFile),waveRecipes(live.waveRecipes),extensionPositions(live.extensionPositions),splitCharacters(live.splitCharacters),imageObjects(live.imageObjects),
  hasCamera(live.hasCamera),hasNameText(live.hasNameText),
  faceType(live.faceType),faceDefaultFile(live.faceDefaultFile),
  assets(live.assets),eyecatchFilm(live.eyecatchFilm),templates(live.templates),cache(live.cache),
  prefetchNativeVideo(live.prefetchNativeVideo){}
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
 auto v=r.str();
 if(k=="@wave-clock")l.waveMilliseconds=std::stoull(v)/1000.;
 else if(k=="@extension-clock")l.extensionMilliseconds=std::stoull(v)/1000.;
 else l.params.emplace(std::move(k),std::move(v));
 }
 for(unsigned i=0;i<l.external.size();++i){
  const auto param=l.params.find("@exv"+std::to_string(i));
  if(param==l.params.end())continue;
  l.external[i]=numeric(param->second);
  l.externalMask|=uint16_t(1u<<i);
 }
 if(sceneCompositePath(l.path)){
 l.composite=sceneComposite(l.path);
 }else if(!l.path.empty()){auto it=cache.find(l.path);
 if(it==cache.end())it=cache.emplace(l.path,loadBitmap(l.path)).first;
 l.pixels=it->second;
 }if(l.composite&&l.params.count("@portrait")&&l.path!=l.params.at("@portrait")){auto portrait=l.params.at("@portrait");auto it=cache.find(portrait);if(it==cache.end())it=cache.emplace(portrait,loadBitmap(portrait)).first;l.pixels=it->second;}
 if(v3&&l.composite)l.composite->restoreIdentity(r);
 if(l.params.count("extfile")&&l.params.count("extmotion")&&l.params.at("extmotion")!="def")
  loadCharacterExtension(l,l.params.at("extfile"));
 if(l.params.count("@replacement"))loadReplacement(l,l.params.at("@replacement"));
 next[name]=std::move(l);
 }r.end();
 layers=std::move(next);
 particleState=savedParticleState;
 waveformCursor=uint32_t(savedWaveformCursor);
 }

}
