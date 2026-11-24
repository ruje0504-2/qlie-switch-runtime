#pragma once
#include "qlie.hpp"
#include <SDL.h>
#include <array>
#include <atomic>
#include "asset_read_ahead.hpp"
namespace whj2 {
struct AudioStats {uint64_t frames,nonzero;};
struct AudioPosition {uint64_t frame=0,length=0;bool loop=false,playing=false;};
class Audio {
public:
 static constexpr unsigned MusicPreviousChannel=4,SettingsTestChannel=5,SystemChannel=24;
 static constexpr unsigned StoryEffect3Channel=25,StoryBgvChannel=26,ChannelCount=27;
private:
 friend struct PreparedAudioRestore;
 struct Channel {DecodedPcm pcm;std::string path;unsigned category=2;size_t pos=0;bool loop=false,stop=false;float gain=0,target=0,step=0;uint64_t fade=0;};
 struct Pending {Channel channel;std::string key;std::optional<uint64_t> seek;unsigned crossfade=0;bool music=false,explicitFade=false;};
 std::array<float,6> userGains{{1,1,1,1,1,1}};
 SDL_AudioDeviceID device=0;std::array<Channel,ChannelCount>channels;std::atomic<uint64_t>frames{0},nonzero{0};
 AssetReadAhead*readAhead=nullptr;
 std::array<std::optional<Pending>,ChannelCount>pending;
 uint64_t unnamedSerial=0;
 static Channel channelFor(unsigned,bool,float,unsigned,const std::string&,unsigned);
 static void changeVolume(Channel&,float,unsigned,bool);
 static void seekTo(Channel&,uint64_t milliseconds);
 void cancelPending(unsigned);
 void queue(unsigned,Pending,std::function<WarmAsset()>);
 void installPending(unsigned,Pending,DecodedPcm);
 uint64_t install(unsigned,Channel,bool,unsigned,bool);
 uint64_t playDecoded(unsigned,DecodedPcm,bool,float,unsigned,const std::string&,unsigned,bool musicCrossfade=false,bool explicitFade=false);
 void clearChannel(unsigned);
 bool fadeMusicToPrevious(unsigned ms,unsigned category,Channel&retired);
 static void callback(void*,Uint8*,int);
#ifdef __SWITCH__
 bool mixerBound=false;
#if QLIE_DIAGNOSTICS
 ThreadBinding mixerBinding;
 int mixerPriorityResult=0;
 std::atomic<bool> mixerBindingReady{false};
 bool mixerBindingReported=false;
#endif
#endif
public:
 void setReadAhead(AssetReadAhead*loader){readAhead=loader;}
 void update();
 void discardQueued(unsigned ch){cancelPending(ch);}
 void warmFile(qlie::Assets&,const std::string&);
 void playFile(qlie::Assets&,unsigned,const std::string&,bool,float,unsigned,unsigned);
 void playMusicFile(qlie::Assets&,const std::string&,bool,float,unsigned,unsigned,bool explicitFade=false);
 static DecodedPcm decodePcm(const qlie::Bytes&,bool allowMissing=false);
 uint64_t playMovie(DecodedPcm,const std::string&);
 void stopMusic(unsigned fadeMs,unsigned category);
 Audio();~Audio();Audio(const Audio&)=delete;Audio&operator=(const Audio&)=delete;
 void play(unsigned,qlie::Bytes,bool,float,unsigned fadeMs=0,const std::string&path="",unsigned category=6);
 void volume(unsigned,float,unsigned fadeMs=0,bool stopAfter=false);
 void setMusicModeGain(float);
 void setUserGains(const std::array<float,5>&);
 qlie::Bytes save();void restore(const qlie::Bytes&,qlie::Assets&);
 static WarmAsset prepareRestore(const qlie::Bytes&,qlie::Assets&);
 void applyRestore(std::shared_ptr<PreparedAudioRestore>,qlie::Assets&);
 AudioPosition position(unsigned);void seek(unsigned,uint64_t milliseconds);void setLoop(unsigned,bool);void stop(unsigned);
 bool playing(unsigned);void stopAll();AudioStats stats()const;
#if defined(__SWITCH__) && QLIE_DIAGNOSTICS
 void reportMixerBinding();
#endif
};
}
