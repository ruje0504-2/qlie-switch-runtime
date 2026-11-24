// SPDX-License-Identifier: GPL-2.0-or-later
#include "cg_catalog.hpp"
#include "image.hpp"
extern "C" {
#include "video.h"
}
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
int main(int argc,char**argv){try{if(argc!=2)return 2;qlie::Assets assets(argv[1]);whj2::CgCatalog catalog(assets);std::set<std::string> seen;unsigned clips=0,silent=0,absent=0,audible=0;
 for(auto&group:catalog.items()){for(auto&file:group.files){if(file.size()>2&&file.substr(file.size()-2)==".b"&&seen.insert(file).second){
 for(auto&m:whj2::abmp(assets.read(file)))if(m.type.rfind("abimg",0)==0&&m.data.size()>4&&std::equal(m.data.begin(),m.data.begin()+4,"OggS")){
 clips++;auto*v=kvideo_open_mode(m.data.data(),m.data.size(),KVIDEO_SOFTWARE);if(!v)throw std::runtime_error("CG movie metadata failed");bool hasAudio=kvideo_has_audio(v);kvideo_close(v);if(!hasAudio){absent++;std::cout<<file<<" no_audio_stream\n";continue;}
 uint8_t*raw=nullptr;size_t bytes=0;if(kaudio_decode(m.data.data(),m.data.size(),&raw,&bytes))throw std::runtime_error("CG audio decode failed");auto*pcm=reinterpret_cast<int16_t*>(raw);size_t nonzero=0;int peak=0;for(size_t i=0;i<bytes/2;i++){nonzero+=pcm[i]!=0;peak=std::max(peak,std::abs(int(pcm[i])));}free(raw);if(nonzero)audible++;else silent++;std::cout<<file<<" frames="<<bytes/4<<" nonzero_samples="<<nonzero<<" peak="<<peak<<"\n";
 }}}}
 std::cout<<"CG audio clips="<<clips<<" silent="<<silent<<" absent="<<absent<<" audible="<<audible<<"\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
