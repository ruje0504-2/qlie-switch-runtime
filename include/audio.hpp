#pragma once
#include "qlie.hpp"
#include <SDL.h>
#include <array>
#include <atomic>
namespace whj2 {
struct AudioStats {uint64_t frames,nonzero;};
class Audio {
 struct Channel {std::vector<int16_t> pcm;std::string path;size_t pos=0;bool loop=false,stop=false;float gain=0,target=0,step=0;uint64_t fade=0;};
 SDL_AudioDeviceID device=0;std::array<Channel,24>channels;std::atomic<uint64_t>frames{0},nonzero{0};
 static void callback(void*,Uint8*,int);
public:
 Audio();~Audio();Audio(const Audio&)=delete;Audio&operator=(const Audio&)=delete;
 void play(unsigned,const qlie::Bytes&,bool,float,unsigned fadeMs=0,const std::string&path="");
 void volume(unsigned,float,unsigned fadeMs=0,bool stopAfter=false);
 qlie::Bytes save();void restore(const qlie::Bytes&,qlie::Assets&);
 bool playing(unsigned);void stopAll();AudioStats stats()const;
};
}
