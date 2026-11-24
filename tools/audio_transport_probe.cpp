// SPDX-License-Identifier: GPL-2.0-or-later
#include "whj2/profile.hpp"
#include "audio.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc,char**argv){try{if(argc!=2)return 2;if(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER))throw std::runtime_error(SDL_GetError());{
 qlie::Assets assets(argv[1],whj2::profile::archives);whj2::Audio audio;audio.play(0,assets.read("music/wbgm01.ogg"),false,1,0,"music/wbgm01.ogg",0);
 auto first=audio.position(0);if(!first.playing||first.length<44100)throw std::runtime_error("Decoded track duration");
 SDL_Delay(100);auto advanced=audio.position(0);if(advanced.frame<=first.frame)throw std::runtime_error("PCM clock not advancing");
 audio.seek(0,12000);auto sought=audio.position(0);if(sought.frame<529200||sought.frame>531248)throw std::runtime_error("Seek not based on sample clock");
 auto state=audio.save();audio.stop(0);if(audio.position(0).length||audio.playing(0))throw std::runtime_error("Channel stop retained data");audio.restore(state,assets);
 auto restored=audio.position(0);if(restored.frame<sought.frame||restored.frame>sought.frame+4096)throw std::runtime_error("Restored transport cursor");
 audio.setLoop(0,true);audio.seek(0,UINT64_MAX);SDL_Delay(80);auto wrapped=audio.position(0);if(!wrapped.loop||!wrapped.playing||wrapped.frame>=44100)throw std::runtime_error("Loop failed to wrap");
 audio.setLoop(0,false);audio.seek(0,UINT64_MAX);if(audio.position(0).frame!=first.length||audio.playing(0))throw std::runtime_error("Clamped EOF seek");
 audio.setUserGains({0,0,0,0,0});audio.setMusicModeGain(1);audio.play(0,assets.read("music/wbgm01.ogg"),false,1,0,"music/wbgm01.ogg",5);auto before=audio.stats();SDL_Delay(100);if(audio.stats().nonzero<=before.nonzero)throw std::runtime_error("Music mode incorrectly used BGM gain");auto dedicated=audio.save();audio.restore(dedicated,assets);if(!audio.playing(0))throw std::runtime_error("Music category restore");auto stats=audio.stats();if(!stats.nonzero)throw std::runtime_error("No real audio output");std::cout<<"PASS PCM transport duration_frames="<<first.length<<" cursor advancement/seek/save-restore/loop/EOF/stop output="<<stats.frames<<" nonzero="<<stats.nonzero<<"\n";
 }SDL_Quit();return 0;}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";SDL_Quit();return 1;}}
