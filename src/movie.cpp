// SPDX-License-Identifier: GPL-2.0-or-later
#include "movie.hpp"
#include <iostream>
#include <algorithm>
namespace whj2 {
Movie::~Movie(){stop();
 }
void Movie::stop(){if(video){std::cerr<<"MOVIE stop decoded_frames="<<kvideo_decoded_frames(video)<<" ticks="<<tick<<" eof="<<ended<<"\n";
 kvideo_close(video);
 video=nullptr;
 audio.volume(7,0,0,true);
 }data.clear();
 }
void Movie::open(const std::string&name){std::string p=qlie::canonical(name);
 if(!assets.exists(p)&&assets.exists(p+".ogv"))p+=".ogv";
 if(!assets.exists(p))throw std::runtime_error("Missing movie "+p);
 openBytes(assets.read(p),p);
 }
void Movie::openBytes(qlie::Bytes b,const std::string&name){stop();
 data=std::move(b);
 video=kvideo_open_mode(data.data(),data.size(),KVIDEO_SOFTWARE);
 if(!video)throw std::runtime_error("Cannot open movie "+name);
 hasAudio=kvideo_has_audio(video);
 if(hasAudio)audio.play(7,data,false,1,0,name);
 origin=audio.stats().frames;
 tick=0;
 ended=false;
 std::fill(frame.pixels.begin(),frame.pixels.end(),0);
 frame.version.publish();
 std::cerr<<"MOVIE start "<<name<<" audio="<<hasAudio<<" audio_clock="<<origin<<"\n";
 }
void Movie::update(){if(!video)return;
 auto target=(audio.stats().frames-origin)*60/44100;
 const auto before=kvideo_written_frames(video);
 KImage dst{0,0,1280,720,1280*4,frame.pixels.data()};
 while(!ended&&tick<=target){uint8_t*pcm=nullptr;
 size_t size=0;
 int rc=kvideo_step(video,&dst,&pcm,&size);
 free(pcm);
 tick++;
 if(rc<0){auto error=std::string(kvideo_error(video));
 stop();
 throw std::runtime_error("Movie decode: "+error);
 }if(rc==1)ended=true;
 }if(kvideo_written_frames(video)!=before)frame.version.publish();
 if(ended&&(!hasAudio||!audio.playing(7)))stop();
 }
}
