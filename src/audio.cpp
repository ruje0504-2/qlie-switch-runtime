// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio.hpp"
#include "state_io.hpp"
#include <cmath>
extern "C" {
#include "video.h"
}
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <iostream>
namespace whj2 {
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
 }std::cerr<<"AUDIO device opened: 44100Hz stereo S16\n";
 SDL_PauseAudioDevice(device,0);
 }
Audio::~Audio(){if(device)SDL_CloseAudioDevice(device);
 }
void Audio::callback(void*context,Uint8*stream,int bytes){auto&a=*static_cast<Audio*>(context);
 auto*out=reinterpret_cast<int16_t*>(stream);
 unsigned count=unsigned(bytes)/4;
 for(unsigned i=0;i<count;i++){float left=0,right=0;
 for(auto&c:a.channels){if(c.pcm.empty())continue;
 if(c.pos+1>=c.pcm.size()){if(c.loop)c.pos=0;
 else continue;
 }if(c.fade){c.gain+=c.step;
 if(!--c.fade)c.gain=c.target;
 }if(c.stop&&!c.fade&&c.gain==0)continue;
 left+=c.pcm[c.pos++]*c.gain;
 right+=c.pcm[c.pos++]*c.gain;
 }int l=std::clamp(int(left),-32768,32767),r=std::clamp(int(right),-32768,32767);
 out[2*i]=int16_t(l);
 out[2*i+1]=int16_t(r);
 if(l||r)a.nonzero.fetch_add(1,std::memory_order_relaxed);
 }a.frames.fetch_add(count,std::memory_order_relaxed);
 }
void Audio::play(unsigned ch,const qlie::Bytes&bytes,bool loop,float gain,unsigned ms,const std::string&path){if(ch>=channels.size())throw std::runtime_error("Audio channel out of range");
 uint8_t*raw=nullptr;
 size_t n=0;
 if(kaudio_decode(bytes.data(),bytes.size(),&raw,&n))throw std::runtime_error("PCM decode failed");
 Channel c;
 c.path=path;
 c.pcm.resize(n/2);
 if(n)std::memcpy(c.pcm.data(),raw,n);
 free(raw);
 c.loop=loop;
 c.target=std::clamp(gain,0.f,1.f);
 c.fade=uint64_t(ms)*44100/1000;
 c.gain=c.fade?0:c.target;
 c.step=c.fade?c.target/float(c.fade):0;
 SDL_LockAudioDevice(device);
 channels[ch]=std::move(c);
 SDL_UnlockAudioDevice(device);
 std::cerr<<"AUDIO play channel="<<ch<<" stereo_frames="<<n/4<<" loop="<<loop<<" gain="<<gain<<"\n";
 }
void Audio::volume(unsigned ch,float gain,unsigned ms,bool stopAfter){if(ch>=channels.size())throw std::runtime_error("Audio channel out of range");
 SDL_LockAudioDevice(device);
 auto&c=channels[ch];
 c.target=std::clamp(gain,0.f,1.f);
 c.fade=uint64_t(ms)*44100/1000;
 c.step=c.fade?(c.target-c.gain)/float(c.fade):0;
 if(!c.fade)c.gain=c.target;
 c.stop=stopAfter;
 SDL_UnlockAudioDevice(device);
 }
void Audio::stopAll(){SDL_LockAudioDevice(device);
 for(auto&c:channels)c=Channel{};
 SDL_UnlockAudioDevice(device);
 }
AudioStats Audio::stats()const{return {frames.load(),nonzero.load()};
 }
qlie::Bytes Audio::save(){StateWriter w;
 w.str("WHJ2-audio-1");
 SDL_LockAudioDevice(device);
 for(auto&c:channels){w.str(c.path);
 w.n(c.pos);
 w.n(c.loop);
 w.n(c.stop);
 w.f(c.gain);
 w.f(c.target);
 w.f(c.step);
 w.n(c.fade);
 }SDL_UnlockAudioDevice(device);
 return w.bytes;
 }
void Audio::restore(const qlie::Bytes&bytes,qlie::Assets&assets){StateReader r{bytes};
 if(r.str()!="WHJ2-audio-1")throw std::runtime_error("Audio state version");
 std::array<Channel,8>next;
 for(auto&c:next){c.path=r.str();
 c.pos=r.n();
 c.loop=r.n()!=0;
 c.stop=r.n()!=0;
 c.gain=r.f();
 c.target=r.f();
 c.step=r.f();
 c.fade=r.n();
 if(!std::isfinite(c.gain)||!std::isfinite(c.target)||!std::isfinite(c.step)||c.gain<0||c.gain>1||c.target<0||c.target>1)throw std::runtime_error("Invalid audio gain state");
 if(!c.path.empty()){auto b=assets.read(c.path);
 uint8_t*raw=nullptr;
 size_t n=0;
 if(kaudio_decode(b.data(),b.size(),&raw,&n))throw std::runtime_error("Restored PCM decode failed");
 c.pcm.resize(n/2);
 if(n)std::memcpy(c.pcm.data(),raw,n);
 free(raw);
 }if(!c.path.empty())std::cerr<<"AUDIO restore path="<<c.path<<" stereo_cursor="<<c.pos/2<<" gain="<<c.gain<<" fade_remaining="<<c.fade<<"\n";
 if(c.pos>c.pcm.size()||c.pos%2)throw std::runtime_error("Invalid audio cursor");
 }r.end();
 SDL_LockAudioDevice(device);
 channels=std::move(next);
 SDL_UnlockAudioDevice(device);
 std::cerr<<"AUDIO restored 8 channels\n";
 }

bool Audio::playing(unsigned n){if(n>=channels.size())throw std::runtime_error("Audio channel index");
 SDL_LockAudioDevice(device);
 auto&c=channels[n];
 bool p=!c.pcm.empty()&&(c.pos<c.pcm.size()||c.loop)&&!(c.stop&&!c.fade&&c.gain==0);
 SDL_UnlockAudioDevice(device);
 return p;
 }

}
