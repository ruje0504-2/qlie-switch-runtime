// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.h"
#include "movie.hpp"
#include "story.hpp"
#include <cstdlib>
#include <iostream>

namespace whj2 {
struct PreparedMovie {
 qlie::Bytes bytes; // KVideo borrows the stored payload until its destructor.
 std::string source;
 std::unique_ptr<KVideo,decltype(&kvideo_close)> video{nullptr,kvideo_close};
 DecodedPcm pcm;
 Image frame{1280,720,qlie::Bytes(1280*720*4)};
 uint64_t tick=0,decodedBeforeRecovery=0,cpuFrameVersion=0;
 bool ended=false,softwareRecovery=false;

 void restartSoftware(uint64_t target,const std::string& reason){
  QLIE_LOGF("MOVIE recovery %s tick=%llu switching=software reason=%s\n",
               source.c_str(),static_cast<unsigned long long>(target),reason.c_str());
  auto*retry=kvideo_reopen_software(video.get(),target);
  if(!retry)throw std::runtime_error("Cannot reopen movie "+source+" at playback time: "+reason);
  if constexpr (qlie::diagnostics) decodedBeforeRecovery=kvideo_decoded_frames(video.get());
  kvideo_disable_audio(retry); // Movie audio stays on its existing mixer clock.
  video.reset(retry);
  softwareRecovery=true;
 }

 void advance(uint64_t target){
  KImage dst{0,0,frame.width,frame.height,frame.width*4,frame.pixels.data()};
  const auto decode=[&]{
   uint8_t*unused=nullptr;size_t size=0;
   int result=kvideo_advance(video.get(),target,&dst,&unused,&size);
   std::free(unused);
   return result;
  };
  auto before=kvideo_written_frames(video.get());
  int result=decode();
  if(result<0){
   KVideoStats stats{};
   const std::string reason=kvideo_error(video.get());
   if(softwareRecovery||kvideo_stats(video.get(),&stats)!=0||(!stats.hardware_pending&&!stats.hardware_frames))
    throw std::runtime_error("Movie "+source+": "+reason);
   restartSoftware(target,reason);
   before=0;
   result=decode();
   if(result<0)throw std::runtime_error("Movie "+source+" software recovery: "+kvideo_error(video.get()));
   if(!kvideo_written_frames(video.get()))throw std::runtime_error("Movie "+source+" did not resume at playback time");
  }
  if(kvideo_written_frames(video.get())!=before)frame.version.publish();
  tick=target+1;
  ended=result==1;
 }
};

namespace {
using Clock=std::chrono::steady_clock;
[[maybe_unused]] double milliseconds(Clock::time_point from,Clock::time_point to){
 return std::chrono::duration<double,std::milli>(to-from).count();
}

std::shared_ptr<PreparedMovie> prepare(qlie::Bytes bytes,const std::string&name,bool nativeSurfaces=false){
 auto movie=std::make_shared<PreparedMovie>();
 movie->bytes=std::move(bytes);
 movie->source=name;
 movie->video.reset(kvideo_open(movie->bytes.data(),movie->bytes.size()));
 if(!movie->video)throw std::runtime_error("Cannot open movie "+name);
 if(nativeSurfaces)kvideo_enable_surface(movie->video.get());
 const bool audio=kvideo_has_audio(movie->video.get());
 if(audio)kvideo_disable_audio(movie->video.get());
 do{movie->advance(movie->tick);}while(!movie->ended&&!kvideo_written_frames(movie->video.get())&&movie->tick<60);
 if(!kvideo_written_frames(movie->video.get()))throw std::runtime_error("Movie has no initial picture "+name);
 if(audio)movie->pcm=Audio::decodePcm(movie->bytes);
 return movie;
}
}

Movie::~Movie(){stop();}

void Movie::stop(){
 if(loading()){
  if(readAhead)readAhead->release(pending);
  pending.clear();
 }
 if(clip){
  QLIE_LOG_LINE(<<"MOVIE stop decoded_frames="<<clip->decodedBeforeRecovery+kvideo_decoded_frames(clip->video.get())
           <<" ticks="<<clip->tick<<" eof="<<clip->ended<<"\n");
  if(started)audio.volume(7,0,0,true);
  clip.reset();
 }
 started=false;
}

std::string Movie::assetPath(const std::string&name)const{
 auto path=trim(name);
 if(path.size()>1&&path.front()=='"'&&path.back()=='"')path=path.substr(1,path.size()-2);
 path=qlie::canonical(path);
 if(!assets.exists(path)&&assets.exists(path+".ogv"))path+=".ogv";
 return path;
}

void Movie::request(const std::string&name,bool required){
 readAhead->request(name,[&assets=assets,name,native=nativeSurfaces]{
  const auto begin=qlie::diagnostics?Clock::now():Clock::time_point{};
  auto bytes=assets.read(name);const auto loaded=qlie::diagnostics?Clock::now():Clock::time_point{};
  WarmAsset ready;ready.movie=prepare(std::move(bytes),name,native);
  ready.bytes=ready.movie->bytes.size()+ready.movie->frame.pixels.size()+ready.movie->pcm.size()*sizeof(int16_t);
  QLIE_LOGF("MOVIE prep %s read_ms=%.3f decode_ms=%.3f\n",name.c_str(),
               milliseconds(begin,loaded),milliseconds(loaded,Clock::now()));
  return ready;
 },required);
}

void Movie::warmFile(const std::string&name){if(readAhead)request(name,false);}

void Movie::takePrepared(){
 if(auto ready=readAhead->takeReady(pending)){
  clip=std::move(ready->movie);
  path=std::move(pending);pending.clear();
 }
}

void Movie::open(const std::string&name){
 auto asset=assetPath(name);
 if(!readAhead){openBytes(assets.read(asset),asset);return;}
 stop();
 if constexpr (qlie::diagnostics) requestedAt=Clock::now();
 pending=std::move(asset);
 request(pending,true);
 takePrepared();
}

void Movie::openBytes(qlie::Bytes bytes,const std::string&name){
 stop();
 if constexpr (qlie::diagnostics) requestedAt=Clock::now();
 clip=prepare(std::move(bytes),name,nativeSurfaces);
 path=name;
}

bool Movie::surface(KVideoSurface&frame)const{
 if(!clip||!kvideo_get_surface(clip->video.get(),&frame))return false;
 frame.version=clip->frame.version.get();return true;
}
const Image&Movie::image()const{
 auto& image=clip->frame;
 const auto version=image.version.get();
 if(clip->cpuFrameVersion!=version&&kvideo_get_surface(clip->video.get(),nullptr)){
  KImage dst{0,0,image.width,image.height,image.width*4,image.pixels.data()};
  if(kvideo_copy_surface(clip->video.get(),&dst)<0){
   const auto target=clip->tick-1;
   clip->restartSoftware(target,kvideo_error(clip->video.get()));
   clip->advance(target);
   if(!kvideo_written_frames(clip->video.get()))throw std::runtime_error("Movie "+path+" did not resume at playback time");
  }
  clip->cpuFrameVersion=image.version.get();
 }
 return image;
}

void Movie::presented(){
 if(!clip||started)return;
 bool audible=!clip->pcm.empty();
 origin=audible?audio.playMovie(std::move(clip->pcm),path):audio.stats().frames;
 started=true;
 QLIE_LOGF("MOVIE start %s audio=%d audio_clock=%llu present_ms=%.3f\n",path.c_str(),int(audible),
              static_cast<unsigned long long>(origin),milliseconds(requestedAt,Clock::now()));
}

void Movie::update(){
 if(loading())takePrepared();
 if(!started)return;
 auto target=(audio.stats().frames-origin)*60/44100;
 if(!clip->ended&&clip->tick<=target)clip->advance(target);
 if(clip->ended&&!audio.playing(7))stop();
}
}
