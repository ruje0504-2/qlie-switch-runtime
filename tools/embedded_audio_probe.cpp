// SPDX-License-Identifier: GPL-2.0-or-later
#include "profiles/whj2.hpp"
#include "scene_images.hpp"
#include "audio.hpp"
#include "state_io.hpp"
#include <iostream>
#include <stdexcept>
int main(int argc,char**argv){try{if(argc!=2)return 2;
 if(SDL_Init(SDL_INIT_AUDIO))throw std::runtime_error(SDL_GetError());
 {qlie::Assets assets(argv[1],whj2::profile::archives);whj2::SceneImages images(assets);whj2::Audio audio;
 images.command("^bg01,file:cut/踏切点灯");auto events=images.takeSoundEvents();
 if(events.size()!=1||events[0].category!=2||events[0].channel!=8||!events[0].loop||events[0].gain!=1)throw std::runtime_error("Original crossing sound event fields");
 if(!images.takeSoundEvents().empty())throw std::runtime_error("Duplicate embedded start");
 audio.play(events[0].channel,events[0].bytes,events[0].loop,events[0].gain,0,events[0].source,events[0].category);
 SDL_Delay(150);auto before=audio.stats();if(!before.nonzero)throw std::runtime_error("No embedded PCM output");
 images.update(150);images.takeSoundEvents();auto imageState=images.save();auto audioState=audio.save();
 whj2::SceneImages restored(assets);restored.restore(imageState);if(!restored.takeSoundEvents().empty())throw std::runtime_error("Restored event retriggered");
 audio.stopAll();audio.restore(audioState,assets);SDL_Delay(150);auto after=audio.stats();
 if(after.nonzero<=before.nonzero||!audio.playing(8))throw std::runtime_error("Embedded audio resume failed");
 whj2::SceneImages delayed(assets);delayed.command("^effect,file:cut/世界崩壊1");
 if(!delayed.takeSoundEvents().empty())throw std::runtime_error("Delayed event fired at start");
 delayed.update(1089);if(!delayed.takeSoundEvents().empty())throw std::runtime_error("Delayed event fired early");
 delayed.update(1);auto event=delayed.takeSoundEvents();if(event.size()!=1||event[0].loop||event[0].gain!=1)throw std::runtime_error("1090ms sound boundary");
 if(!delayed.takeSoundEvents().empty())throw std::runtime_error("Repeated delayed event");
 whj2::SceneImages categorized(assets);categorized.command("^bg01,file:cut/踏切点灯,voltype:voice");auto voiceType=categorized.takeSoundEvents();if(voiceType.size()!=1||voiceType[0].category!=1)throw std::runtime_error("Image volume category");
 whj2::SceneImages muted(assets);muted.command("^bg04,file:cut/踏切点灯,vol:0,show:false");
 auto silent=muted.takeSoundEvents();if(silent.size()!=1||!silent[0].stop||silent[0].channel!=8)throw std::runtime_error("Original muted crossing scene");
 restored.command("^bg01,vol:0");auto pending=restored.save();whj2::SceneImages pendingRestore(assets);pendingRestore.restore(pending);auto stop=pendingRestore.takeSoundEvents();if(stop.size()!=1||!stop[0].stop||stop[0].channel!=8)throw std::runtime_error("Image volume zero must stop direct bus");
 audio.volume(stop[0].channel,0,0,true);if(audio.playing(8))throw std::runtime_error("Direct bus still playing");
 whj2::SceneImages channels(assets);channels.command("^bg01,file:cut/踏切点灯,dchannel:15,vol:50");auto distinct=channels.takeSoundEvents();
 if(distinct.size()!=1||distinct[0].channel!=23||distinct[0].gain!=.5f)throw std::runtime_error("Direct bus offset or object gain");
 audio.play(distinct[0].channel,distinct[0].bytes,true,distinct[0].gain,0,distinct[0].source);
 auto directState=audio.save();audio.stopAll();audio.restore(directState,assets);if(!audio.playing(23))throw std::runtime_error("Direct bus 15 restoration");
 // Convert the current channel-8 state into the original eight-channel schema.
 whj2::StateReader reader{audioState};if(reader.str()!="WHJ2-audio-3"||reader.n()!=whj2::Audio::ChannelCount)throw std::runtime_error("Current audio schema");
 std::vector<qlie::Bytes> records;
 for(unsigned i=0;i<whj2::Audio::ChannelCount;++i){auto path=reader.str();reader.n();auto start=reader.p;for(unsigned n=0;n<7;++n)reader.n();whj2::StateWriter oldRecord;oldRecord.str(path);oldRecord.bytes.insert(oldRecord.bytes.end(),audioState.begin()+start,audioState.begin()+reader.p);records.push_back(oldRecord.bytes);}reader.end();
 whj2::StateWriter v2;v2.str("WHJ2-audio-2");v2.n(24);for(unsigned i=0;i<24;i++)v2.bytes.insert(v2.bytes.end(),records[i].begin(),records[i].end());audio.restore(v2.bytes,assets);if(!audio.playing(8))throw std::runtime_error("Audio-2 restore");
 whj2::StateWriter legacy;legacy.str("WHJ2-audio-1");for(unsigned i=0;i<8;++i){auto&record=records[i==4?8:i];legacy.bytes.insert(legacy.bytes.end(),record.begin(),record.end());}
 audio.restore(legacy.bytes,assets);if(!audio.playing(8)||audio.playing(4))throw std::runtime_error("Legacy embedded channel migration");
 audio.setUserGains({0,0,0,0,0});SDL_Delay(80);auto mutedCount=audio.stats().nonzero;SDL_Delay(100);if(audio.stats().nonzero!=mutedCount)throw std::runtime_error("User mixer mute");audio.setUserGains({1,1,1,1,1});SDL_Delay(100);if(audio.stats().nonzero<=mutedCount)throw std::runtime_error("User mixer unmute");
 auto crossing=events[0];audio.play(20,crossing.bytes,true,.5f,0,crossing.source,2);SDL_Delay(80);
 auto channelRecord=[](const qlie::Bytes&b,unsigned target){whj2::StateReader r{b};r.str();auto count=r.n();qlie::Bytes result;for(unsigned i=0;i<count;i++){auto start=r.p;r.str();for(unsigned j=0;j<8;j++)r.n();if(i==target)result.assign(b.begin()+start,b.begin()+r.p);}r.end();return result;};
 auto beforeUi=audio.save();auto oldDirect=channelRecord(beforeUi,20);whj2::StateReader oldReader{oldDirect};auto directPath=oldReader.str();oldReader.n();auto oldPos=oldReader.n();
 auto uiPath=std::string("misc/Sys_OnClick.wav");audio.play(whj2::Audio::SystemChannel,assets.read(uiPath),false,1,0,uiPath,3);SDL_Delay(80);auto both=audio.save();auto direct=channelRecord(both,20),ui=channelRecord(both,whj2::Audio::SystemChannel);whj2::StateReader dr{direct},ur{ui};if(dr.str()!=directPath||dr.n()!=2||dr.n()<=oldPos||ur.str()!=uiPath||ur.n()!=3)throw std::runtime_error("System audio replaced direct12 or its cursor");
 audio.play(20,crossing.bytes,true,.25f,0,crossing.source,2);if(channelRecord(audio.save(),whj2::Audio::SystemChannel).empty()||!audio.playing(whj2::Audio::SystemChannel))throw std::runtime_error("Direct12 replaced UI audio");
 audio.restore(both,assets);if(!audio.playing(20)||!audio.playing(whj2::Audio::SystemChannel))throw std::runtime_error("Two-channel save restore");
 whj2::StateReader oldCountReader{both};oldCountReader.str();oldCountReader.n();whj2::StateWriter old24;old24.str("WHJ2-audio-3");old24.n(24);for(unsigned i=0;i<24;i++){auto record=channelRecord(both,i);old24.bytes.insert(old24.bytes.end(),record.begin(),record.end());}audio.restore(old24.bytes,assets);if(!audio.playing(20)||audio.playing(whj2::Audio::SystemChannel))throw std::runtime_error("24-channel audio-3 migration");
 whj2::StateWriter legacyUi;legacyUi.str("WHJ2-audio-3");legacyUi.n(24);for(unsigned i=0;i<24;i++){auto record=channelRecord(both,i==20?whj2::Audio::SystemChannel:i);legacyUi.bytes.insert(legacyUi.bytes.end(),record.begin(),record.end());}audio.restore(legacyUi.bytes,assets);if(audio.playing(20)||!audio.playing(whj2::Audio::SystemChannel))throw std::runtime_error("Legacy UI slot migration");
 std::cout<<"PASS direct12/UI isolation, advancing cursor, two-channel restore, 24-channel migration\n";
 std::cout<<"PASS category gains mute/unmute and audio-2 restore\n";
 std::cout<<"PASS legacy audio migration and pending mute restoration\n";
 std::cout<<"PASS direct bus 0/15, object gain, muted original scene and zero-volume stop\n";
 std::cout<<"PASS embedded start/default volume/loop, exact delayed boundary, scene cursor restore, PCM source restore\n";
 std::cout<<"CoreAudio callback frames="<<after.frames<<" nonzero="<<after.nonzero<<" resumed_nonzero="<<after.nonzero-before.nonzero<<"\n";
 }SDL_Quit();return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";SDL_Quit();return 1;}}
