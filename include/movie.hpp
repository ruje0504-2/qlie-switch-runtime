#pragma once
#include "image.hpp"
#include "audio.hpp"
extern "C" {
#include "video.h"
}
namespace whj2 {
class Movie {
 qlie::Assets&assets;Audio&audio;qlie::Bytes data;KVideo*video=nullptr;Image frame{1280,720,qlie::Bytes(1280*720*4)};uint64_t origin=0,tick=0;bool ended=false,hasAudio=false;
public:
 Movie(qlie::Assets&a,Audio&s):assets(a),audio(s){}~Movie();
 void open(const std::string&);void openBytes(qlie::Bytes,const std::string&);void update();void stop();bool playing()const{return video!=nullptr;}uint64_t elapsedMs()const{return video?(audio.stats().frames-origin)*1000/44100:0;}const Image&image()const{return frame;}
};
}
