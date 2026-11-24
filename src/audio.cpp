// SPDX-License-Identifier: GPL-2.0-or-later
#include "diagnostics.h"
#include "audio.hpp"
#include "state_io.hpp"
#include "image.hpp"
#include <cmath>
#include <climits>
#include <cstdio>
extern "C" {
#include "video.h"
}
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <sstream>
namespace whj2 {
namespace {
WarmAsset preparePcm(const qlie::Bytes&bytes){
 WarmAsset ready;ready.pcm=Audio::decodePcm(bytes);
 ready.bytes=ready.pcm.size()*sizeof(int16_t);
 return ready;
}
}
Audio::Audio(){SDL_AudioSpec wanted{},got{};
 wanted.freq=44100;
 wanted.format=AUDIO_S16SYS;
 wanted.channels=2;
 wanted.samples=1024;
 wanted.callback=callback;
 wanted.userdata=this;
 device=SDL_OpenAudioDevice(nullptr,0,&wanted,&got,0);
 if(!device)throw std::runtime_error(SDL_GetError());
 if(got.freq!=44100||got.channels!=2||got.format!=AUDIO_S16SYS){SDL_CloseAudioDevice(device);
 device=0;
 throw std::runtime_error("Unexpected PCM output format");
 }QLIE_LOG_LINE(<<"AUDIO device opened: 44100Hz stereo S16\n");
 SDL_PauseAudioDevice(device,0);
 }
Audio::~Audio(){if(device)SDL_CloseAudioDevice(device);
 }
void Audio::callback(void*context,Uint8*stream,int bytes){auto&a=*static_cast<Audio*>(context);
#ifdef __SWITCH__
 if(!a.mixerBound){
  a.mixerBound=true;
#if QLIE_DIAGNOSTICS
  // The linked Switch SDL maps TIME_CRITICAL to background priority.
  a.mixerBinding=bindSwitchThread(2);
  a.mixerPriorityResult=SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
  a.mixerBindingReady.store(true,std::memory_order_release);
#else
  bindSwitchThread(2);
  SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);
#endif
 }
#endif
 auto*out=reinterpret_cast<int16_t*>(stream);
 unsigned count=unsigned(bytes)/4;
 std::array<Channel*,ChannelCount> active;
 unsigned activeCount=0;
 for(auto&channel:a.channels){
  if(!channel.pcm.empty())active[activeCount++]=&channel;
 }
 unsigned audibleFrames=0;
 for(unsigned i=0;i<count;i++){float left=0,right=0;
 for(unsigned channel=0;channel<activeCount;++channel){auto&c=*active[channel];
 if(c.pos+1>=c.pcm.size()){if(c.loop)c.pos=0;
 else continue;
 }if(c.fade){c.gain+=c.step;
 if(!--c.fade)c.gain=c.target;
 }if(c.stop&&!c.fade&&c.gain==0)continue;
 left+=c.pcm[c.pos++]*c.gain*a.userGains[c.category];
 right+=c.pcm[c.pos++]*c.gain*a.userGains[c.category];
 }int l=std::clamp(int(left),-32768,32767),r=std::clamp(int(right),-32768,32767);
 out[2*i]=int16_t(l);
 out[2*i+1]=int16_t(r);
 if constexpr (qlie::diagnostics) audibleFrames+=l!=0||r!=0;
 }
 if constexpr (qlie::diagnostics) a.nonzero.fetch_add(audibleFrames,std::memory_order_relaxed);
 a.frames.fetch_add(count,std::memory_order_relaxed);
 }
#if defined(__SWITCH__) && QLIE_DIAGNOSTICS
void Audio::reportMixerBinding(){
 if(!mixerBindingReported&&mixerBindingReady.load(std::memory_order_acquire)){
  logSwitchThread("audio",mixerBinding);
  QLIE_LOGF("AUDIO mixer_priority=high priority_set_rc=%d\n",mixerPriorityResult);
  mixerBindingReported=true;
 }
}
#endif
DecodedPcm Audio::decodePcm(const qlie::Bytes&bytes,bool allowMissing){
 uint8_t*raw=nullptr;size_t n=0;
 const auto result=kaudio_decode(bytes.data(),bytes.size(),&raw,&n);
 if(result==KAUDIO_NO_STREAM&&allowMissing)return {};
 if(result)throw std::runtime_error("PCM decode failed");
 return {std::shared_ptr<const int16_t>(reinterpret_cast<const int16_t*>(raw),[](const int16_t*p){std::free(const_cast<int16_t*>(p));}),n/2};
}
void Audio::warmFile(qlie::Assets&assets,const std::string&path){
 if(readAhead)readAhead->request(qlie::canonical(path),[&assets,path]{return preparePcm(assets.read(path));});
}
void Audio::playFile(qlie::Assets&assets,unsigned ch,const std::string&path,bool loop,float gain,unsigned ms,unsigned category){
 if(!readAhead){playDecoded(ch,decodePcm(assets.read(path)),loop,gain,ms,path,category);return;}
 queue(ch,{channelFor(ch,loop,gain,ms,path,category),qlie::canonical(path),{}},
       [&assets,path]{return preparePcm(assets.read(path));});
}
void Audio::playMusicFile(qlie::Assets&assets,const std::string&path,bool loop,float gain,unsigned ms,unsigned category,bool explicitFade){
 if(!readAhead){playDecoded(0,decodePcm(assets.read(path)),loop,gain,ms,path,category,true,explicitFade);return;}
 queue(0,{channelFor(0,loop,gain,ms,path,category),qlie::canonical(path),{},ms,true,explicitFade},
       [&assets,path]{return preparePcm(assets.read(path));});
}
void Audio::play(unsigned ch,qlie::Bytes bytes,bool loop,float gain,unsigned ms,const std::string&path,unsigned category){
 if(!readAhead){playDecoded(ch,decodePcm(bytes),loop,gain,ms,path,category);return;}
 // Embedded audio locators are unique per container member. Unnamed inputs
 // use the channel's pending request and own their input bytes until decode.
 auto key=path.empty()?"@pcm:"+std::to_string(++unnamedSerial):qlie::canonical(path);
 queue(ch,{channelFor(ch,loop,gain,ms,path,category),std::move(key),{}},
       [bytes=std::move(bytes)]{return preparePcm(bytes);});
}
uint64_t Audio::playMovie(DecodedPcm pcm,const std::string&path){
 return playDecoded(7,std::move(pcm),false,1,0,path,4);
}
uint64_t Audio::playDecoded(unsigned ch,DecodedPcm pcm,bool loop,float gain,unsigned ms,const std::string&path,unsigned category,bool musicCrossfade,bool explicitFade){
 if(ch>=channels.size())throw std::runtime_error("Invalid PCM channel");
 cancelPending(ch);
 auto c=channelFor(ch,loop,gain,ms,path,category);c.pcm=std::move(pcm);
 return install(ch,std::move(c),musicCrossfade,ms,explicitFade);
}
Audio::Channel Audio::channelFor(unsigned ch,bool loop,float gain,unsigned ms,const std::string&path,unsigned category){
 Channel c;c.path=path;
 c.category=category<6?category:ch==0?0:ch==3?1:ch==7?4:2;
 c.loop=loop;
 c.target=std::clamp(gain,0.f,1.f);
 c.fade=uint64_t(ms)*44100/1000;
 c.gain=c.fade?0:c.target;
 c.step=c.fade?c.target/float(c.fade):0;
 return c;
}
uint64_t Audio::install(unsigned ch,Channel c,bool musicCrossfade,unsigned ms,bool explicitFade){
 if(c.pcm.empty())throw std::runtime_error("Empty PCM clip "+c.path);
#if QLIE_DIAGNOSTICS
 const size_t stereoFrames=c.pcm.size()/2;
 const bool loop=c.loop;const float gain=c.target;const auto path=c.path;
#endif
 Channel retiredPrevious;
 // Keep PCM and string destruction out of SDL's callback lock.
 SDL_LockAudioDevice(device);
 if(musicCrossfade){
  const bool hadMusic=fadeMusicToPrevious(ms,c.category,retiredPrevious);
  if(!hadMusic&&!explicitFade){c.fade=0;c.step=0;c.gain=c.target;}
 }
 std::swap(channels[ch],c);
 const auto firstFrame=frames.load(std::memory_order_relaxed);
 SDL_UnlockAudioDevice(device);
#if QLIE_DIAGNOSTICS
 std::ostringstream line;
 line<<"AUDIO play channel="<<ch<<" stereo_frames="<<stereoFrames<<" loop="<<loop
     <<" gain="<<gain<<" path="<<path<<'\n';
 const auto message=line.str();
 std::fwrite(message.data(),1,message.size(),stderr);
#endif
 return firstFrame;
 }
void Audio::cancelPending(unsigned ch){
 if(!pending[ch])return;
 auto key=std::move(pending[ch]->key);pending[ch].reset();
 if(readAhead&&std::none_of(pending.begin(),pending.end(),[&](const auto&p){return p&&p->key==key;}))readAhead->release(key);
}
void Audio::queue(unsigned ch,Pending request,std::function<WarmAsset()> load){
 if(ch>=channels.size())throw std::runtime_error("Invalid PCM channel");
 if(!pending[ch]||pending[ch]->key!=request.key)cancelPending(ch);
 readAhead->request(request.key,std::move(load),true);
 const bool keepMusic=request.music;
 pending[ch]=std::move(request);
 if(!keepMusic)clearChannel(ch);
 update();
}
void Audio::installPending(unsigned ch,Pending clip,DecodedPcm pcm){
 clip.channel.pcm=std::move(pcm);
 if(clip.seek)seekTo(clip.channel,*clip.seek);
 install(ch,std::move(clip.channel),clip.music,clip.crossfade,clip.explicitFade);
}
void Audio::update(){
 if(!readAhead)return;
 for(const auto&entry:pending){
  if(!entry)continue;
  auto ready=readAhead->takeReady(entry->key);
  if(!ready)continue;
  auto key=entry->key;
  for(unsigned ch=0;ch<pending.size();++ch)if(pending[ch]&&pending[ch]->key==key){
   auto clip=std::move(*pending[ch]);pending[ch].reset();
   installPending(ch,std::move(clip),ready->pcm);
  }
 }
}
void Audio::clearChannel(unsigned ch){
 Channel retired;
 SDL_LockAudioDevice(device);
 std::swap(channels[ch],retired);
 SDL_UnlockAudioDevice(device);
}
bool Audio::fadeMusicToPrevious(unsigned ms,unsigned category,Channel&retired){
 auto&current=channels[0];
 auto&previous=channels[MusicPreviousChannel];
 std::swap(previous,retired);
 const bool audible=current.gain>0&&(current.loop||current.pos+1<current.pcm.size());
 if(ms&&audible){
  previous=std::move(current);
  previous.category=category;
  previous.target=0;
  previous.stop=true;
  previous.fade=uint64_t(ms)*44100/1000;
  previous.step=-previous.gain/float(previous.fade);
 }
 return audible;
}
void Audio::stopMusic(unsigned ms,unsigned category){
 cancelPending(0);
 Channel retiredPrevious,retiredCurrent;
 SDL_LockAudioDevice(device);
 fadeMusicToPrevious(ms,category,retiredPrevious);
 std::swap(channels[0],retiredCurrent);
 SDL_UnlockAudioDevice(device);
}
void Audio::changeVolume(Channel&c,float gain,unsigned ms,bool stopAfter){
 c.target=std::clamp(gain,0.f,1.f);
 c.fade=uint64_t(ms)*44100/1000;
 c.step=c.fade?(c.target-c.gain)/float(c.fade):0;
 if(!c.fade)c.gain=c.target;
 c.stop=stopAfter;
}
void Audio::volume(unsigned ch,float gain,unsigned ms,bool stopAfter){if(ch>=channels.size())throw std::runtime_error("Audio channel out of range");
 if(pending[ch]){
  if(stopAfter){cancelPending(ch);}
  else{changeVolume(pending[ch]->channel,gain,ms,false);pending[ch]->explicitFade=true;return;}
 }
 SDL_LockAudioDevice(device);
 changeVolume(channels[ch],gain,ms,stopAfter);
 SDL_UnlockAudioDevice(device);
 }
void Audio::setMusicModeGain(float gain){if(!std::isfinite(gain)||gain<0||gain>1)throw std::runtime_error("Music mode gain");SDL_LockAudioDevice(device);userGains[5]=gain;SDL_UnlockAudioDevice(device);}
void Audio::setUserGains(const std::array<float,5>&gains){for(float gain:gains)if(!std::isfinite(gain)||gain<0||gain>1)throw std::runtime_error("Invalid category gain");SDL_LockAudioDevice(device);std::copy(gains.begin(),gains.end(),userGains.begin());SDL_UnlockAudioDevice(device);}
void Audio::stopAll(){
 for(unsigned ch=0;ch<pending.size();++ch)cancelPending(ch);
 std::array<Channel,ChannelCount> retired;
 SDL_LockAudioDevice(device);
 channels.swap(retired);
 SDL_UnlockAudioDevice(device);
 }
AudioStats Audio::stats()const{return {frames.load(),nonzero.load()};
 }
qlie::Bytes Audio::save(){StateWriter w;
 auto writeChannel=[&](const Channel&c){
  w.str(c.path);w.n(c.category);w.n(c.pos);w.n(c.loop);w.n(c.stop);
  w.f(c.gain);w.f(c.target);w.f(c.step);w.n(c.fade);
 };
 const auto queued=std::count_if(pending.begin(),pending.end(),[](const auto&clip){return clip&&!clip->channel.path.empty();});
 w.str(queued?"WHJ2-audio-4":"WHJ2-audio-3");
 w.n(channels.size());
 // Keep the audible tracks and their cursors, even while a replacement is queued.
 SDL_LockAudioDevice(device);
 for(const auto&channel:channels)writeChannel(channel);
 SDL_UnlockAudioDevice(device);
 if(queued){
  w.n(queued);
  for(unsigned ch=0;ch<pending.size();++ch){
   if(!pending[ch]||pending[ch]->channel.path.empty())continue;
   const auto&clip=*pending[ch];
   w.n(ch);writeChannel(clip.channel);
   w.n(clip.seek.has_value());if(clip.seek)w.n(*clip.seek);
   w.n(clip.crossfade);w.n(clip.music);w.n(clip.explicitFade);
  }
 }
 return w.bytes;
 }
namespace {
qlie::Bytes audioSource(qlie::Assets&assets,const std::string&path){
 if(path.rfind("@abmp:",0)!=0)return assets.read(path);
 auto split=path.find(':',6);if(split==std::string::npos)throw std::runtime_error("Embedded audio locator");
 size_t index=std::stoul(path.substr(6,split-6)),current=0;
 for(auto&member:abmp(assets.read(path.substr(split+1))))if(member.type.rfind("absnd",0)==0){if(current++==index)return member.data;}
 throw std::runtime_error("Embedded audio member not found");
}
}
struct PreparedAudioRestore {
 std::array<Audio::Channel,Audio::ChannelCount> channels;
 std::array<std::optional<Audio::Pending>,Audio::ChannelCount> queued;
};
WarmAsset Audio::prepareRestore(const qlie::Bytes&bytes,qlie::Assets&assets){
 StateReader r{bytes};
 auto version=r.str();bool legacy=version=="WHJ2-audio-1";
 bool withQueue=version=="WHJ2-audio-4";
 bool categories=withQueue||version=="WHJ2-audio-3";
 if(!legacy&&!categories&&version!="WHJ2-audio-2")throw std::runtime_error("Audio state version");
 auto count=legacy?8:r.n();if(count>ChannelCount)throw std::runtime_error("Audio state channel count");
 WarmAsset result;
 result.audioRestore=std::make_shared<PreparedAudioRestore>();
 auto&next=result.audioRestore->channels;
 auto&queued=result.audioRestore->queued;
 auto readChannel=[&](size_t index){
  Channel c;c.path=r.str();
  auto category=categories?r.n():index==0?0:index==3?1:index==7?4:2;
  if(category>5)throw std::runtime_error("Audio category");
  c.category=unsigned(category);c.pos=r.n();c.loop=r.n()!=0;c.stop=r.n()!=0;
  c.gain=r.f();c.target=r.f();c.step=r.f();c.fade=r.n();
  if(!std::isfinite(c.gain)||!std::isfinite(c.target)||!std::isfinite(c.step)||c.gain<0||c.gain>1||c.target<0||c.target>1)
   throw std::runtime_error("Invalid audio gain state");
  return c;
 };
 for(size_t index=0;index<count;++index)next[index]=readChannel(index);
 if(withQueue){
  auto queuedCount=r.n();if(queuedCount>queued.size())throw std::runtime_error("Queued audio count");
  while(queuedCount--){
   auto ch=r.n();if(ch>=queued.size()||queued[ch])throw std::runtime_error("Queued audio channel");
   Pending clip;clip.channel=readChannel(ch);
   if(clip.channel.path.empty()||clip.channel.pos)throw std::runtime_error("Invalid queued audio source or cursor");
   clip.key=qlie::canonical(clip.channel.path);
   if(r.n())clip.seek=r.n();
   auto fade=r.n();clip.music=r.n()!=0;clip.explicitFade=r.n()!=0;
   if(fade>UINT_MAX||(clip.music&&ch!=0))throw std::runtime_error("Invalid queued audio crossfade");
   clip.crossfade=unsigned(fade);queued[ch]=std::move(clip);
  }
 }
 r.end();
 for(auto&c:next){
  if(!c.path.empty()){
   c.pcm=decodePcm(audioSource(assets,c.path));
   result.bytes+=c.pcm.size()*sizeof(int16_t);
  }
  if(c.pos>c.pcm.size()||c.pos%2)throw std::runtime_error("Invalid audio cursor");
 }
 if(legacy)for(unsigned i=4;i<=6;++i)if(next[i].path.rfind("@abmp:",0)==0){next[i+4]=std::move(next[i]);next[i]=Channel{};}
 if(categories&&count==24&&next[20].category==3&&next[20].path.rfind("misc/Sys_",0)==0){next[SystemChannel]=std::move(next[20]);next[20]=Channel{};}
 return result;
}
void Audio::restore(const qlie::Bytes&bytes,qlie::Assets&assets){
 if(readAhead){
  constexpr auto key="@restore:sync";
  readAhead->request(key,[bytes,&assets]{return prepareRestore(bytes,assets);},true);
  applyRestore(std::move(readAhead->take(key)->audioRestore),assets);
 }else applyRestore(prepareRestore(bytes,assets).audioRestore,assets);
}
void Audio::applyRestore(std::shared_ptr<PreparedAudioRestore> next,qlie::Assets&assets){
 auto&queued=next->queued;
 if(!readAhead)for(auto&clip:queued)if(clip)clip->channel.pcm=decodePcm(audioSource(assets,clip->channel.path));
#if QLIE_DIAGNOSTICS
 std::ostringstream log;
 for(const auto&c:next->channels)if(!c.path.empty())
  log<<"AUDIO restore path="<<c.path<<" stereo_cursor="<<c.pos/2<<" gain="<<c.gain<<" fade_remaining="<<c.fade<<'\n';
#endif
 for(unsigned ch=0;ch<pending.size();++ch)cancelPending(ch);
 SDL_LockAudioDevice(device);
 channels.swap(next->channels);
 SDL_UnlockAudioDevice(device);
 for(unsigned ch=0;ch<queued.size();++ch)if(queued[ch]){
  auto clip=std::move(*queued[ch]);
  if(readAhead){
   const auto path=clip.channel.path;
   queue(ch,std::move(clip),[&assets,path]{return preparePcm(audioSource(assets,path));});
  }else{
   auto pcm=std::move(clip.channel.pcm);
   installPending(ch,std::move(clip),std::move(pcm));
  }
 }
 if(readAhead)readAhead->retire(std::move(next));
#if QLIE_DIAGNOSTICS
 log<<"AUDIO restored "<<channels.size()<<" channels\n";
 const auto message=log.str();
 std::fwrite(message.data(),1,message.size(),stderr);
#endif
}

AudioPosition Audio::position(unsigned ch){if(ch>=channels.size())throw std::runtime_error("Audio channel index");
 if(pending[ch])return {0,0,pending[ch]->channel.loop,true};
 SDL_LockAudioDevice(device);const auto&c=channels[ch];AudioPosition result{c.pos/2,c.pcm.size()/2,c.loop,!c.pcm.empty()&&(c.pos<c.pcm.size()||c.loop)&&!(c.stop&&!c.fade&&c.gain==0)};SDL_UnlockAudioDevice(device);return result;
}
void Audio::seekTo(Channel&c,uint64_t milliseconds){
 const uint64_t length=c.pcm.size()/2;
 auto frame=milliseconds>length*1000/44100?length:milliseconds*44100/1000;
 c.pos=size_t(std::min(frame,length))*2;
}
void Audio::seek(unsigned ch,uint64_t milliseconds){if(ch>=channels.size())throw std::runtime_error("Audio channel index");
 if(pending[ch]){pending[ch]->seek=milliseconds;return;}
 SDL_LockAudioDevice(device);seekTo(channels[ch],milliseconds);SDL_UnlockAudioDevice(device);
}
void Audio::setLoop(unsigned ch,bool loop){if(ch>=channels.size())throw std::runtime_error("Audio channel index");if(pending[ch]){pending[ch]->channel.loop=loop;return;}SDL_LockAudioDevice(device);channels[ch].loop=loop;SDL_UnlockAudioDevice(device);}
void Audio::stop(unsigned ch){if(ch>=channels.size())throw std::runtime_error("Audio channel index");cancelPending(ch);clearChannel(ch);}

bool Audio::playing(unsigned n){if(n>=channels.size())throw std::runtime_error("Audio channel index");
 if(pending[n])return true;
 SDL_LockAudioDevice(device);
 auto&c=channels[n];
 bool p=!c.pcm.empty()&&(c.pos<c.pcm.size()||c.loop)&&!(c.stop&&!c.fade&&c.gain==0);
 SDL_UnlockAudioDevice(device);
 return p;
 }

}
