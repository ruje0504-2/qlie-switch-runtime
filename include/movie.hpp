#pragma once
#include "image.hpp"
#include "audio.hpp"
#include <chrono>
extern "C" {
#include "video.h"
}
namespace whj2 {
class Movie {
 qlie::Assets&assets;Audio&audio;
 std::shared_ptr<PreparedMovie> clip;
 std::string pending,path,primed;
 std::chrono::steady_clock::time_point requestedAt;
 uint64_t origin=0;
 bool started=false,nativeSurfaces=false;
 AssetReadAhead*readAhead=nullptr;
 void request(const std::string&,bool);
 void takePrepared();
public:
 Movie(qlie::Assets&a,Audio&s):assets(a),audio(s){}~Movie();
 void setReadAhead(AssetReadAhead*loader){readAhead=loader;}
 void setNativeSurfaces(bool enabled){nativeSurfaces=enabled;}
 std::string assetPath(const std::string&)const;
 void warmFile(const std::string&);
 void primeFile(const std::string&);
 void open(const std::string&);void openBytes(qlie::Bytes,const std::string&);
 void update();void presented();void stop();
 bool loading()const{return !pending.empty();}
 bool hasFrame()const{return bool(clip);}
 bool playing()const{return hasFrame()||loading();}
 uint64_t elapsedMs()const{return started?(audio.stats().frames-origin)*1000/44100:0;}
 const Image&image()const;
 bool surface(KVideoSurface&)const;
};
}
